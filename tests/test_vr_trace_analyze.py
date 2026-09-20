import importlib.util
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "vr_trace_analyze.py"
SPEC = importlib.util.spec_from_file_location("vr_trace_analyze", MODULE_PATH)
MOD = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
import sys
sys.modules[SPEC.name] = MOD
SPEC.loader.exec_module(MOD)


class VrTraceAnalyzeTests(unittest.TestCase):
    def test_parse_and_rank_varying_matrix(self):
        lines = [
            "noise\n",
            "VRTRACE frame=1 event=1 kind=uniform program=7 index=0 location=3 type=0x8b5c size=1 name=ViewProjection\n",
            "VRTRACE frame=1 event=2 kind=mat4 program=7 fbo=0 location=3 count=1 transpose=0 viewport=0,0,1280,720 m=[1 0 0 0 | 0 1 0 0 | 0 0 1 0 | 0 0 0 1]\n",
            "VRTRACE frame=1 event=3 kind=draw-elements draw=1 program=7 fbo=0 mode=0x0004 count=36 type=0x1403 viewport=0,0,1280,720\n",
            "VRTRACE frame=2 event=4 kind=mat4 program=7 fbo=0 location=3 count=1 transpose=0 viewport=0,0,1280,720 m=[1 0 0 0 | 0 1 0 0 | 0 0 1 0 | 1 0 0 1]\n",
            "VRTRACE frame=2 event=5 kind=draw-elements draw=2 program=7 fbo=0 mode=0x0004 count=36 type=0x1403 viewport=0,0,1280,720\n",
            "VRTRACE frame=2 event=6 kind=mat4 program=9 fbo=1 location=2 count=1 transpose=0 viewport=0,0,256,256 m=[2 0 0 0 | 0 2 0 0 | 0 0 2 0 | 0 0 0 1]\n",
        ]
        events = MOD.parse_events(lines)
        self.assertEqual(len(events), 6)
        summary = MOD.summarize(events)
        self.assertEqual(summary["frame_count"], 2)
        self.assertEqual(summary["draws_by_program"]["7"], 2)
        self.assertEqual(summary["draws_by_fbo"]["0"], 2)

        candidate = summary["matrix_candidates"][0]
        self.assertEqual(candidate["program"], "7")
        self.assertEqual(candidate["location"], "3")
        self.assertEqual(candidate["name"], "ViewProjection")
        self.assertEqual(candidate["unique_matrices"], 2)
        self.assertGreater(candidate["max_delta_from_first"], 0.0)

    def test_rejects_invalid_matrix(self):
        event = MOD.Event(
            frame=1,
            event=1,
            kind="mat4",
            body="program=1 location=2 m=[1 2 3]",
        )
        summary = MOD.summarize([event])
        self.assertEqual(summary["matrix_candidates"], [])

    def test_cli_rejects_log_without_trace(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "empty.log"
            path.write_text("nothing useful\n", encoding="utf-8")
            self.assertEqual(MOD.main([str(path)]), 3)


if __name__ == "__main__":
    unittest.main()
