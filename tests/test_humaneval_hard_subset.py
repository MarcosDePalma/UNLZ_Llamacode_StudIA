import json
import tempfile
import unittest
from pathlib import Path

from tools.select_humaneval_hard import complexity, load_jsonl, rank_tasks, write_subset


def sample(number: int, body: str, tests: str = "assert candidate(1) == 1") -> dict:
    return {
        "task_id": f"HumanEval/{number}",
        "prompt": "def candidate(x):\n",
        "canonical_solution": body,
        "test": tests,
        "entry_point": "candidate",
    }


class HumanEvalHardSubsetTests(unittest.TestCase):
    def test_structural_complexity_outranks_one_liner(self):
        easy = sample(20, "    return x\n")
        hard = sample(21, "    for i in range(x):\n        if i % 2:\n            x -= i\n    return x\n")
        self.assertGreater(complexity(hard)[0], complexity(easy)[0])

    def test_ranking_excludes_seen_prefix_and_is_deterministic(self):
        tasks = [sample(3, "    return x\n"), sample(22, "    return x\n"), sample(21, "    return x\n")]
        ranked = rank_tasks(tasks, exclude_before=20)
        self.assertEqual([item.task_number for item in ranked], [21, 22])

    def test_writer_preserves_original_rows_and_writes_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.jsonl"
            rows = [sample(20, "    return x\n"), sample(21, "    if x:\n        return x\n    return 0\n")]
            source.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
            output = root / "hard.jsonl"
            manifest = root / "manifest.json"
            write_subset(source, output, manifest, count=1, exclude_before=20)
            selected = load_jsonl(output)
            self.assertEqual(selected, [rows[1]])
            metadata = json.loads(manifest.read_text(encoding="utf-8"))
            self.assertEqual(metadata["tasks"][0]["task_id"], "HumanEval/21")

    def test_invalid_task_id_and_missing_fields_fail_closed(self):
        with self.assertRaises(ValueError):
            rank_tasks([{**sample(20, "    return x\n"), "task_id": "bad"}])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "broken.jsonl"
            path.write_text('{"task_id":"HumanEval/20"}\n', encoding="utf-8")
            with self.assertRaises(ValueError):
                load_jsonl(path)


if __name__ == "__main__":
    unittest.main()
