#!/usr/bin/env python3
"""Create a short, deterministic and locally validated BigCodeBench-Hard pack."""

from __future__ import annotations

import argparse
import ast
import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path


UNSAFE_LIBS = {
    "cgi", "ftplib", "getpass", "http", "requests", "smtplib", "socket",
    "subprocess", "urllib",
}


def parse_libs(value: object) -> list[str]:
    if isinstance(value, list):
        return [str(item) for item in value]
    parsed = ast.literal_eval(str(value))
    if not isinstance(parsed, list) or not all(isinstance(item, str) for item in parsed):
        raise ValueError(f"invalid libs field: {value!r}")
    return parsed


def locally_eligible(libs: list[str]) -> tuple[bool, str]:
    blocked = sorted(set(libs) & UNSAFE_LIBS)
    if blocked:
        return False, "unsafe/network/process libs: " + ", ".join(blocked)
    missing = sorted(lib for lib in libs if importlib.util.find_spec(lib) is None)
    if missing:
        return False, "missing libs: " + ", ".join(missing)
    return True, ""


def wrapped_tests(test_source: str, entry_point: str) -> str:
    return (
        test_source.rstrip()
        + "\n\nimport sys\n"
        + "def check(candidate):\n"
        + f"    globals()[{entry_point!r}] = candidate\n"
        + "    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])\n"
        + "    result = unittest.TestResult()\n"
        + "    suite.run(result)\n"
        + "    if not result.wasSuccessful():\n"
        + "        details = [str(error) for _, error in result.failures + result.errors]\n"
        + "        raise AssertionError(' | '.join(details))\n"
        + f"\ncheck({entry_point})\n"
    )


def canonical_program(row: dict) -> str:
    return (
        str(row["code_prompt"]).rstrip()
        + "\n"
        + str(row["canonical_solution"]).rstrip()
        + "\n\n"
        + wrapped_tests(str(row["test"]), str(row["entry_point"]))
    )


def validate_canonical(row: dict, timeout: int = 30) -> tuple[bool, str]:
    with tempfile.TemporaryDirectory() as directory:
        candidate = Path(directory) / "candidate.py"
        candidate.write_text(canonical_program(row), encoding="utf-8")
        try:
            run = subprocess.run(
                [sys.executable, "-I", str(candidate)], cwd=directory,
                capture_output=True, text=True, timeout=timeout, encoding="utf-8",
            )
        except subprocess.TimeoutExpired:
            return False, f"canonical timeout after {timeout}s"
    if run.returncode == 0:
        return True, ""
    detail = (run.stderr or run.stdout).strip().splitlines()
    return False, detail[-1][:300] if detail else f"exit {run.returncode}"


def deterministic_order(rows: list[dict], seed: str) -> list[dict]:
    return sorted(
        rows,
        key=lambda row: hashlib.sha256(
            f"{seed}:{row['task_id']}".encode("utf-8")
        ).hexdigest(),
    )


def select_rows(rows: list[dict], count: int, seed: str) -> tuple[list[dict], list[dict]]:
    selected: list[dict] = []
    rejected: list[dict] = []
    for row in deterministic_order(rows, seed):
        libs = parse_libs(row["libs"])
        eligible, reason = locally_eligible(libs)
        if eligible:
            eligible, reason = validate_canonical(row)
        if not eligible:
            rejected.append({"task_id": row["task_id"], "reason": reason})
            continue
        selected.append(row)
        if len(selected) == count:
            break
    if len(selected) != count:
        raise ValueError(f"only {len(selected)} locally valid tasks; requested {count}")
    return selected, rejected


def make_pack(rows: list[dict]) -> dict:
    items = []
    for row in rows:
        entry = str(row["entry_point"])
        # Use the official English Instruct prompt verbatim.  Adding a translated
        # wrapper changes the task (and is especially unfair to models whose chat
        # template was tuned on the published English benchmark).
        prompt = str(row["instruct_prompt"]).strip()
        items.append({
            "id": row["task_id"],
            "prompt": prompt,
            "type": "code_tests",
            "tests": wrapped_tests(str(row["test"]), entry),
            "preamble": str(row["code_prompt"]),
            "entryPoint": entry,
        })
    return {
        "id": "bigcodebench-hard-short",
        "name": f"BigCodeBench-Hard ({len(items)} ítems)",
        "source": "https://huggingface.co/datasets/bigcode/bigcodebench-hard",
        "license": "Apache-2.0",
        "items": items,
    }


def load_parquet(path: Path) -> list[dict]:
    import pandas as pd

    return pd.read_parquet(path).to_dict(orient="records")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--count", type=int, default=8)
    parser.add_argument("--seed", default="llamacode-bcb-hard-v1")
    args = parser.parse_args()
    if args.count <= 0:
        raise ValueError("count must be positive")
    rows, rejected = select_rows(load_parquet(args.source), args.count, args.seed)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(make_pack(rows), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    manifest = args.manifest or args.output.with_suffix(".manifest.json")
    manifest.write_text(json.dumps({
        "source": str(args.source.resolve()), "seed": args.seed,
        "selection": "seeded hash; unsafe/missing dependencies excluded; canonical tests pass locally",
        "tasks": [{"task_id": row["task_id"], "libs": parse_libs(row["libs"])} for row in rows],
        "rejected_before_selection_completed": rejected,
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("selected:", ", ".join(row["task_id"] for row in rows))
    print("pack:", args.output)
    print("manifest:", manifest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
