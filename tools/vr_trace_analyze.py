#!/usr/bin/env python3
"""Summarize Open Citadel VRTRACE logs for stereo/camera reverse engineering.

The parser is intentionally donor-agnostic. It only consumes diagnostic text
emitted by the open compatibility host.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

EVENT_RE = re.compile(
    r"^VRTRACE frame=(?P<frame>\d+) event=(?P<event>\d+) "
    r"kind=(?P<kind>\S+)(?: (?P<body>.*))?$"
)
KV_RE = re.compile(r"(\w[\w-]*)=([^\s]+)")
MATRIX_RE = re.compile(r"\bm=\[(.*)\]\s*$")


@dataclass(frozen=True)
class Event:
    frame: int
    event: int
    kind: str
    body: str


def parse_events(lines: Iterable[str]) -> list[Event]:
    events: list[Event] = []
    for raw in lines:
        match = EVENT_RE.match(raw.strip())
        if not match:
            continue
        events.append(
            Event(
                frame=int(match.group("frame")),
                event=int(match.group("event")),
                kind=match.group("kind"),
                body=match.group("body") or "",
            )
        )
    return events


def parse_kv(body: str) -> dict[str, str]:
    # Matrix text contains spaces and pipes, but scalar key/value fields precede
    # it and are still safely found by this expression.
    return {m.group(1): m.group(2) for m in KV_RE.finditer(body)}


def parse_matrix(body: str) -> tuple[float, ...] | None:
    match = MATRIX_RE.search(body)
    if not match:
        return None
    tokens = match.group(1).replace("|", " ").split()
    try:
        values = tuple(float(token) for token in tokens)
    except ValueError:
        return None
    if len(values) != 16 or not all(math.isfinite(v) for v in values):
        return None
    return values


def matrix_delta(a: tuple[float, ...], b: tuple[float, ...]) -> float:
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def summarize(events: list[Event]) -> dict:
    kind_counts = Counter(event.kind for event in events)
    frames = sorted({event.frame for event in events})

    draws_by_program: Counter[str] = Counter()
    draws_by_fbo: Counter[str] = Counter()
    viewport_counts: Counter[str] = Counter()
    program_uniform_names: dict[str, dict[str, str]] = defaultdict(dict)

    matrices: dict[tuple[str, str], list[tuple[int, tuple[float, ...]]]] = defaultdict(list)

    for event in events:
        kv = parse_kv(event.body)
        if event.kind in {"draw-arrays", "draw-elements"}:
            draws_by_program[kv.get("program", "?")] += 1
            draws_by_fbo[kv.get("fbo", "?")] += 1
            viewport_counts[kv.get("viewport", "?")] += 1
        elif event.kind in {"uniform", "uniform-location"}:
            program = kv.get("program", "?")
            location = kv.get("location", "?")
            name = kv.get("name")
            if name:
                program_uniform_names[program][location] = name
        elif event.kind == "mat4":
            matrix = parse_matrix(event.body)
            if matrix is None:
                continue
            key = (kv.get("program", "?"), kv.get("location", "?"))
            matrices[key].append((event.frame, matrix))

    matrix_candidates = []
    for (program, location), samples in matrices.items():
        first = samples[0][1]
        unique_rounded = {
            tuple(round(value, 6) for value in matrix) for _, matrix in samples
        }
        max_delta = max((matrix_delta(first, matrix) for _, matrix in samples), default=0.0)
        changed_frames = len(
            {
                frame
                for frame, matrix in samples
                if matrix_delta(first, matrix) > 1e-5
            }
        )
        name = program_uniform_names.get(program, {}).get(location)
        matrix_candidates.append(
            {
                "program": program,
                "location": location,
                "name": name,
                "samples": len(samples),
                "unique_matrices": len(unique_rounded),
                "frames": len({frame for frame, _ in samples}),
                "changed_frames_from_first": changed_frames,
                "max_delta_from_first": max_delta,
            }
        )

    # Camera/projection candidates are likely to recur across frames and vary.
    matrix_candidates.sort(
        key=lambda item: (
            item["changed_frames_from_first"] > 0,
            item["frames"],
            item["unique_matrices"],
            item["samples"],
            item["max_delta_from_first"],
        ),
        reverse=True,
    )

    return {
        "events": len(events),
        "frame_count": len(frames),
        "first_frame": frames[0] if frames else None,
        "last_frame": frames[-1] if frames else None,
        "kind_counts": dict(kind_counts.most_common()),
        "draws_by_program": dict(draws_by_program.most_common()),
        "draws_by_fbo": dict(draws_by_fbo.most_common()),
        "viewports": dict(viewport_counts.most_common()),
        "matrix_candidates": matrix_candidates,
    }


def print_human(summary: dict, limit: int) -> None:
    print(
        f"events={summary['events']} frames={summary['frame_count']} "
        f"range={summary['first_frame']}..{summary['last_frame']}"
    )
    print("event kinds:")
    for kind, count in summary["kind_counts"].items():
        print(f"  {kind:18} {count}")

    print("draws by framebuffer:")
    for fbo, count in list(summary["draws_by_fbo"].items())[:limit]:
        print(f"  fbo={fbo:8} draws={count}")

    print("draws by program:")
    for program, count in list(summary["draws_by_program"].items())[:limit]:
        print(f"  program={program:8} draws={count}")

    print("matrix candidates (variation is evidence, not proof of camera semantics):")
    for candidate in summary["matrix_candidates"][:limit]:
        name = candidate["name"] or "<unknown>"
        print(
            "  program={program} location={location} name={name} "
            "samples={samples} frames={frames} unique={unique_matrices} "
            "changed_frames={changed_frames_from_first} max_delta={max_delta_from_first:.6g}".format(
                name=name, **candidate
            )
        )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("--json", action="store_true", dest="as_json")
    parser.add_argument("--limit", type=int, default=30)
    args = parser.parse_args(argv)

    try:
        with args.log.open("r", encoding="utf-8", errors="replace") as handle:
            events = parse_events(handle)
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if not events:
        print("error: no VRTRACE events found", file=sys.stderr)
        return 3

    result = summarize(events)
    if args.as_json:
        json.dump(result, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    else:
        print_human(result, max(1, args.limit))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
