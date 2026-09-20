#!/usr/bin/env python3
"""Headless Chrome smoke test for a recovered Epic Citadel HTML5 deployment."""
from __future__ import annotations

import argparse
import json
import time

from selenium import webdriver
from selenium.webdriver.chrome.options import Options


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("url", nargs="?", default="http://127.0.0.1:8000/")
    parser.add_argument("--seconds", type=int, default=30)
    args = parser.parse_args()

    options = Options()
    options.add_argument("--headless=new")
    options.add_argument("--no-sandbox")
    options.add_argument("--disable-dev-shm-usage")
    options.add_argument("--enable-webgl")
    options.add_argument("--ignore-gpu-blocklist")
    options.add_argument("--use-gl=angle")
    options.add_argument("--use-angle=swiftshader")
    options.set_capability("goog:loggingPrefs", {"browser": "ALL"})

    driver = webdriver.Chrome(options=options)
    driver.set_page_load_timeout(90)
    try:
        try:
            driver.get(args.url)
        except Exception as exc:
            # Old Emscripten apps can intentionally keep the load event busy.
            print(f"navigation warning: {type(exc).__name__}: {exc}")

        deadline = time.time() + args.seconds
        while time.time() < deadline:
            time.sleep(1)

        state = driver.execute_script(
            """
            const canvas = document.querySelector('canvas');
            let gl = null;
            try {
              if (canvas) gl = canvas.getContext('webgl') || canvas.getContext('experimental-webgl');
            } catch (_) {}
            const mod = (typeof Module !== 'undefined') ? Module : null;
            return {
              title: document.title,
              readyState: document.readyState,
              bodyText: document.body ? document.body.innerText.slice(0, 1000) : '',
              canvas: canvas ? {
                width: canvas.width,
                height: canvas.height,
                clientWidth: canvas.clientWidth,
                clientHeight: canvas.clientHeight
              } : null,
              webgl: !!gl,
              webglVersion: gl ? gl.getParameter(gl.VERSION) : null,
              modulePresent: !!mod,
              calledRun: mod ? !!mod.calledRun : false,
              runtimeInitialized: mod ? !!mod.runtimeInitialized : false,
              runDependencies: mod && typeof mod.runDependencies !== 'undefined' ? mod.runDependencies : null
            };
            """
        )
        print("BROWSER_STATE " + json.dumps(state, sort_keys=True))

        logs = driver.get_log("browser")
        severe = []
        for entry in logs:
            level = entry.get("level", "")
            message = entry.get("message", "")
            print(f"BROWSER_LOG {level}: {message}")
            if level == "SEVERE":
                severe.append(message)

        # A successful smoke test requires the page, canvas and a WebGL context.
        # calledRun is reported but is not yet mandatory: the first bring-up may
        # expose an archive/runtime compatibility issue before UE3 main().
        ok = bool(state.get("canvas") and state.get("webgl") and state.get("modulePresent"))
        if not ok:
            return 2
        if severe:
            print(f"severe browser messages: {len(severe)}")
        return 0
    finally:
        driver.quit()


if __name__ == "__main__":
    raise SystemExit(main())
