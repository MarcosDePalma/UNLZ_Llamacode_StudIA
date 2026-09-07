"""Regression tests for the external KV-cache A/B runner."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from tools.kv_cache_ab import (
    compare_receipts,
    compare_run_records,
    normalize_server_args,
    _log_tail,
    summarize_deltas,
    validate_config,
)


def receipt(*, exact: bool = True, latency: float = 100.0,
            prompt_tps: float = 10.0, gen_tps: float = 20.0) -> dict:
    return {
        "summary": {"allPassed": exact},
        "results": [{
            "id": "case-1",
            "stream": "user-000",
            "contextTokens": 8192,
            "depth": 0.5,
            "exactPasskey": exact,
            "containsPasskey": exact,
            "latencyMs": latency,
            "promptTps": prompt_tps,
            "genTps": gen_tps,
        }],
    }


class KvCacheAbTests(unittest.TestCase):
    def test_normalize_server_args_canonicalizes_owned_flags(self):
        normalized = normalize_server_args([
            "--ctx-size", "131072", "--model", "old.gguf", "--port=9999",
            "--host", "0.0.0.0", "--cache-type-k", "q8_0",
        ], "new model.gguf", "127.0.0.1", 18081)
        self.assertNotIn("old.gguf", normalized)
        self.assertNotIn("--port=9999", normalized)
        self.assertEqual(normalized[-7:], [
            "--metrics", "--model", "new model.gguf", "--host", "127.0.0.1", "--port", "18081",
        ])

    def test_summary_marks_statistical_winner_only_with_significant_ci(self):
        summary = summarize_deltas([-1.0, 1.0, -0.5, 0.5])
        self.assertEqual(summary["count"], 4)
        self.assertAlmostEqual(summary["medianPct"], 0.0)
        self.assertFalse(summary["significant"])
        self.assertEqual(summary["winner"], "within-noise")

        clear = summarize_deltas([20.0, 21.0, 19.0, 20.5, 20.2])
        self.assertTrue(clear["significant"])
        self.assertEqual(clear["winner"], "candidate")

    def test_compare_receipts_pairs_same_fixture_and_reports_deltas(self):
        result = compare_receipts(
            [receipt(latency=100, prompt_tps=10, gen_tps=20)],
            [receipt(latency=80, prompt_tps=12, gen_tps=25)],
        )
        self.assertEqual(result["pairCount"], 1)
        self.assertEqual(result["candidateExactPasses"], 1)
        self.assertAlmostEqual(result["latency"]["medianPct"], -20.0)
        self.assertAlmostEqual(result["promptTps"]["medianPct"], 20.0)
        self.assertAlmostEqual(result["genTps"]["medianPct"], 25.0)

    def test_compare_run_records_preserves_pass_numbers_after_failed_pass(self):
        baseline = [
            {"pass": 1, "receipt": receipt()},
            {"pass": 2, "error": "server crash"},
            {"pass": 3, "receipt": receipt(latency=110)},
        ]
        candidate = [
            {"pass": 1, "receipt": receipt(latency=90)},
            {"pass": 2, "error": "server crash"},
            {"pass": 3, "receipt": receipt(latency=100)},
        ]
        result = compare_run_records(baseline, candidate)
        self.assertEqual(result["pairCount"], 2)
        self.assertEqual([row["pass"] for row in result["pairedRows"]], [1, 3])

    def test_validate_config_requires_two_distinct_variants(self):
        config = {
            "serverExe": "llama-server",
            "modelPath": "model.gguf",
            "commonArgs": [],
            "variants": [
                {"id": "same", "args": [], "env": {}},
                {"id": "same", "args": [], "env": {}},
            ],
        }
        with self.assertRaisesRegex(ValueError, "duplicado"):
            validate_config(config)

    def test_validate_config_accepts_absolute_wsl_paths(self):
        config = {
            "serverExe": "/home/test/runtime/llama-server",
            "modelPath": "/home/test/model.gguf",
            "launcher": {
                "kind": "wsl",
                "distro": "Ubuntu-22.04",
                "cwd": "/home/test/runtime",
            },
            "variants": [
                {"id": "baseline", "args": [], "env": {}},
                {"id": "candidate", "args": [], "env": {}},
            ],
        }
        normalized = validate_config(config)
        self.assertEqual(normalized["launcher"]["kind"], "wsl")
        self.assertEqual(normalized["launcher"]["distro"], "Ubuntu-22.04")

    def test_validate_config_allows_a_server_per_variant(self):
        config = {
            "serverExe": "baseline-server",
            "modelPath": "model.gguf",
            "variants": [
                {"id": "baseline", "args": [], "env": {}},
                {"id": "candidate", "serverExe": "kv-stream-server",
                 "args": ["--kv-stream-stage-mib", "2048"], "env": {}},
            ],
        }
        normalized = validate_config(config)
        self.assertEqual(normalized["variants"][0]["serverExe"], "baseline-server")
        self.assertEqual(normalized["variants"][1]["serverExe"], "kv-stream-server")

    def test_validate_config_rejects_relative_wsl_paths(self):
        config = {
            "serverExe": "runtime/llama-server",
            "modelPath": "/home/test/model.gguf",
            "launcher": {"kind": "wsl", "distro": "Ubuntu-22.04"},
            "variants": [
                {"id": "baseline", "args": [], "env": {}},
                {"id": "candidate", "args": [], "env": {}},
            ],
        }
        with self.assertRaisesRegex(ValueError, "ruta absoluta Linux"):
            validate_config(config)

    def test_log_tail_keeps_startup_failure_actionable_and_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "server.log"
            path.write_bytes(("prefix\n" + "x" * 5000 + "\nCUDA failure\n").encode())
            tail = _log_tail(path, max_bytes=128)
            self.assertLessEqual(len(tail.encode("utf-8")), 128)
            self.assertIn("CUDA failure", tail)


if __name__ == "__main__":
    unittest.main()
