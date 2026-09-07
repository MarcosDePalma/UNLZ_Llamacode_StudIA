"""Regression tests for the vendor-neutral external harness matrix runner."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.harness_matrix import PROTOCOL, load_suite, plan_invocations, tool_call_score


ROOT = Path(__file__).resolve().parents[1]
MATRIX = ROOT / "tools" / "harness_matrix.py"
MULTIDOMAIN = ROOT / "assets" / "benchmarks" / "custom" / "harness_multidomain_v1.json"
TOOL_CONTRACT = ROOT / "assets" / "benchmarks" / "custom" / "harness_tool_contract_v1.json"


class HarnessMatrixTests(unittest.TestCase):
    def test_multidomain_suite_is_versioned_and_covers_distinct_domains(self):
        suite = load_suite(MULTIDOMAIN)
        self.assertEqual(suite["id"], "harness_multidomain_v1")
        self.assertGreaterEqual(len(suite["tasks"]), 8)
        self.assertGreaterEqual(
            len({task["category"] for task in suite["tasks"]}), 8)
        for task in suite["tasks"]:
            self.assertTrue(task["id"])
            self.assertTrue(task["prompt"])
            self.assertIsInstance(task["acceptance"], dict)

    def test_tool_contract_scores_order_success_and_redundancy(self):
        suite = load_suite(TOOL_CONTRACT)
        task = suite["tasks"][0]
        expected = task["acceptance"]["toolCalls"]
        calls = [
            {"tool": "read_file", "arguments": {"path": "README.md"},
             "completed": True, "ok": True},
            {"tool": "write_file", "arguments": {"path": "tool_contract.txt"},
             "completed": True, "ok": True},
        ]
        score = tool_call_score(calls, expected)
        self.assertEqual(score["f1Pct"], 100.0)
        self.assertTrue(score["sequenceExact"])
        duplicated = tool_call_score(calls + [calls[1]], expected)
        self.assertEqual(duplicated["redundantCalls"], 1)
        self.assertFalse(duplicated["sequenceExact"])

    def test_invocation_plan_is_seeded_and_balanced(self):
        adapters = [{"id": "a"}, {"id": "b"}]
        tasks = [{"id": "t1"}, {"id": "t2"}, {"id": "t3"}]
        first = plan_invocations(adapters, tasks, 3, 17)
        second = plan_invocations(adapters, tasks, 3, 17)
        self.assertEqual(first, second)
        for pass_number in range(1, 4):
            batch = [item for item in first if item["pass"] == pass_number]
            self.assertEqual(len(batch), 6)
            self.assertCountEqual([item["adapterId"] for item in batch], ["a"] * 3 + ["b"] * 3)
            self.assertCountEqual([item["task"]["id"] for item in batch], ["t1", "t2", "t3"] * 2)

    def test_cli_runs_fake_adapter_and_persists_pairable_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            adapter = root / "fake_adapter.py"
            adapter.write_text(
                "import json, sys\n"
                f"PROTOCOL = {PROTOCOL!r}\n"
                "for line in sys.stdin:\n"
                "    request = json.loads(line)\n"
                "    expected = request['task'].get('acceptance', {}).get('toolCalls', [])\n"
                "    calls = [{'tool': item['tool'], 'arguments': item.get('arguments', {}), 'completed': True, 'ok': True} for item in expected]\n"
                "    print(json.dumps({'protocol': PROTOCOL, 'requestId': request['requestId'], 'ok': True, 'passed': True, 'elapsedMs': 7, 'toolCalls': calls}))\n",
                encoding="utf-8",
            )
            suite = root / "suite.json"
            suite.write_text(json.dumps({
                "id": "mini",
                "name": "Mini",
                "prompts": [{
                    "id": "contract",
                    "category": "tool-contract",
                    "prompt": "do it",
                    "acceptance": {"toolCalls": [{"tool": "read_file", "arguments": {"path": "a"}}]},
                }],
            }), encoding="utf-8")
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({
                "adapters": [{"id": "fake", "command": [sys.executable, str(adapter)]}],
            }), encoding="utf-8")
            output = root / "report.json"
            result = subprocess.run(
                [sys.executable, str(MATRIX), "--suite", str(suite),
                 "--manifest", str(manifest), "--passes", "2", "--seed", "9",
                 "--out", str(output)],
                cwd=ROOT, text=True, capture_output=True, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["invocationCount"], 2)
            self.assertEqual(report["adapters"][0]["taskSuccessRatePct"], 100.0)
            self.assertEqual(report["rows"][0]["toolCallQuality"]["f1Pct"], 100.0)
            self.assertEqual(len(report["orderByPass"]), 2)

    def test_cli_fails_closed_on_wrong_adapter_protocol(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            adapter = root / "bad_adapter.py"
            adapter.write_text(
                "import json, sys\n"
                "for line in sys.stdin:\n"
                "    request = json.loads(line)\n"
                "    print(json.dumps({'protocol': 'wrong', 'requestId': request['requestId']}))\n",
                encoding="utf-8",
            )
            suite = root / "suite.json"
            suite.write_text(json.dumps({
                "id": "mini", "prompts": [{"id": "t", "prompt": "do"}],
            }), encoding="utf-8")
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({
                "adapters": [{"id": "bad", "command": [sys.executable, str(adapter)]}],
            }), encoding="utf-8")
            output = root / "report.json"
            result = subprocess.run(
                [sys.executable, str(MATRIX), "--suite", str(suite),
                 "--manifest", str(manifest), "--passes", "1", "--out", str(output)],
                cwd=ROOT, text=True, capture_output=True, check=False,
            )
            self.assertEqual(result.returncode, 1)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(len(report["errors"]), 1)
            self.assertFalse(report["rows"][0]["invocationOk"])

    def test_cli_fails_closed_on_non_finite_elapsed_time(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            adapter = root / "nan_adapter.py"
            adapter.write_text(
                "import json, sys\n"
                f"PROTOCOL = {PROTOCOL!r}\n"
                "for line in sys.stdin:\n"
                "    request = json.loads(line)\n"
                "    print(json.dumps({'protocol': PROTOCOL, 'requestId': request['requestId'], 'ok': True, 'elapsedMs': 'NaN'}))\n",
                encoding="utf-8",
            )
            suite = root / "suite.json"
            suite.write_text(json.dumps({
                "id": "mini", "prompts": [{"id": "t", "prompt": "do"}],
            }), encoding="utf-8")
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({
                "adapters": [{"id": "nan", "command": [sys.executable, str(adapter)]}],
            }), encoding="utf-8")
            output = root / "report.json"
            result = subprocess.run(
                [sys.executable, str(MATRIX), "--suite", str(suite),
                 "--manifest", str(manifest), "--passes", "1", "--out", str(output)],
                cwd=ROOT, text=True, capture_output=True, check=False,
            )
            self.assertEqual(result.returncode, 1)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertIn("invalid elapsedMs", report["errors"][0])
            self.assertFalse(report["rows"][0]["invocationOk"])


if __name__ == "__main__":
    unittest.main()
