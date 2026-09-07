import unittest
from unittest.mock import patch

from tools.prepare_bigcodebench_hard import (
    deterministic_order,
    locally_eligible,
    make_pack,
    parse_libs,
    select_rows,
    wrapped_tests,
)


def row(task_id: str) -> dict:
    return {
        "task_id": task_id,
        "libs": "['json']",
        "code_prompt": "import json\ndef task_func(x):\n",
        "canonical_solution": "    return x\n",
        "test": "import unittest\nclass TestCases(unittest.TestCase):\n    def test_x(self): self.assertEqual(task_func(1), 1)\n",
        "entry_point": "task_func",
        "instruct_prompt": "Return the input.",
    }


class BigCodeBenchPreparationTests(unittest.TestCase):
    def test_lib_parsing_and_safety_filter(self):
        self.assertEqual(parse_libs("['json', 'numpy']"), ["json", "numpy"])
        self.assertTrue(locally_eligible(["json"])[0])
        self.assertFalse(locally_eligible(["requests"])[0])
        with self.assertRaises((ValueError, SyntaxError)):
            parse_libs("not a list")

    def test_order_is_seeded_and_deterministic(self):
        rows = [row("BigCodeBench/2"), row("BigCodeBench/1"), row("BigCodeBench/3")]
        first = [item["task_id"] for item in deterministic_order(rows, "seed")]
        self.assertEqual(first, [item["task_id"] for item in deterministic_order(rows, "seed")])
        self.assertCountEqual(first, [item["task_id"] for item in rows])

    def test_tests_are_executed_through_check(self):
        tests = wrapped_tests(row("BigCodeBench/1")["test"], "task_func")
        self.assertIn("import sys", tests)
        self.assertIn("loadTestsFromModule", tests)
        self.assertIn("check(task_func)", tests)

    @patch("tools.prepare_bigcodebench_hard.validate_canonical", return_value=(True, ""))
    def test_selection_and_pack_preserve_public_prompt(self, _validate):
        rows = [row("BigCodeBench/1"), row("BigCodeBench/2")]
        selected, rejected = select_rows(rows, 1, "seed")
        self.assertEqual(rejected, [])
        pack = make_pack(selected)
        self.assertEqual(len(pack["items"]), 1)
        self.assertIn("Return the input.", pack["items"][0]["prompt"])
        self.assertNotIn("canonical_solution", pack["items"][0])


if __name__ == "__main__":
    unittest.main()
