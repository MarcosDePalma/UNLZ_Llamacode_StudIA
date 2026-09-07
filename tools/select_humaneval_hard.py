#!/usr/bin/env python3
"""Build a short, deterministic HumanEval subset biased toward code complexity.

The canonical solution is used only to rank tasks. Output rows remain the original
public HumanEval records, so LlamaCode still prompts with the statement and grades
with the official tests; the reference solution is never sent to the model.
"""

from __future__ import annotations

import argparse
import ast
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


CONTROL_FLOW = (ast.If, ast.For, ast.AsyncFor, ast.While, ast.Try, ast.With, ast.AsyncWith)
COMPREHENSIONS = (ast.ListComp, ast.SetComp, ast.DictComp, ast.GeneratorExp)


@dataclass(frozen=True)
class RankedTask:
    score: int
    task_number: int
    task: dict
    metrics: dict[str, int]


def task_number(task_id: str) -> int:
    prefix, sep, suffix = task_id.rpartition("/")
    if not sep or not prefix or not suffix.isdigit():
        raise ValueError(f"invalid HumanEval task_id: {task_id!r}")
    return int(suffix)


def _depth(node: ast.AST, level: int = 0) -> int:
    children = list(ast.iter_child_nodes(node))
    if not children:
        return level
    return max(_depth(child, level + 1) for child in children)


def complexity(task: dict) -> tuple[int, dict[str, int]]:
    prompt = task.get("prompt", "")
    solution = task.get("canonical_solution", "")
    tests = task.get("test", "")
    tree = ast.parse(prompt + solution)
    nodes = list(ast.walk(tree))
    statements = sum(isinstance(node, ast.stmt) for node in nodes)
    control_flow = sum(isinstance(node, CONTROL_FLOW) for node in nodes)
    comprehensions = sum(isinstance(node, COMPREHENSIONS) for node in nodes)
    bool_ops = sum(isinstance(node, (ast.BoolOp, ast.Compare)) for node in nodes)
    calls = sum(isinstance(node, ast.Call) for node in nodes)
    depth = _depth(tree)
    asserts = tests.count("assert ")
    solution_lines = sum(bool(line.strip()) for line in solution.splitlines())
    prompt_words = len(prompt.split())

    # Structural work dominates; prompt/test size only breaks close ties.
    score = (
        statements * 3
        + control_flow * 9
        + comprehensions * 7
        + bool_ops * 2
        + calls
        + depth * 2
        + asserts
        + solution_lines * 2
        + min(prompt_words // 20, 8)
    )
    metrics = {
        "statements": statements,
        "control_flow": control_flow,
        "comprehensions": comprehensions,
        "depth": depth,
        "asserts": asserts,
        "solution_lines": solution_lines,
    }
    return score, metrics


def rank_tasks(tasks: Iterable[dict], exclude_before: int = 20) -> list[RankedTask]:
    ranked: list[RankedTask] = []
    for task in tasks:
        number = task_number(task.get("task_id", ""))
        if number < exclude_before:
            continue
        score, metrics = complexity(task)
        ranked.append(RankedTask(score, number, task, metrics))
    return sorted(ranked, key=lambda item: (-item.score, item.task_number))


def load_jsonl(path: Path) -> list[dict]:
    rows: list[dict] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        row = json.loads(line)
        required = {"task_id", "prompt", "canonical_solution", "test", "entry_point"}
        missing = required - row.keys()
        if missing:
            raise ValueError(f"line {line_number}: missing {sorted(missing)}")
        rows.append(row)
    return rows


def write_subset(source: Path, output: Path, manifest: Path, count: int, exclude_before: int) -> None:
    ranked = rank_tasks(load_jsonl(source), exclude_before)
    if count <= 0 or count > len(ranked):
        raise ValueError(f"count must be between 1 and {len(ranked)}")
    selected = ranked[:count]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        "".join(json.dumps(item.task, ensure_ascii=False) + "\n" for item in selected),
        encoding="utf-8",
    )
    manifest.write_text(
        json.dumps(
            {
                "source": str(source.resolve()),
                "selection": "canonical AST complexity; official prompt/tests unchanged",
                "exclude_before": exclude_before,
                "count": count,
                "tasks": [
                    {"task_id": item.task["task_id"], "score": item.score, **item.metrics}
                    for item in selected
                ],
            },
            ensure_ascii=False,
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--count", type=int, default=12)
    parser.add_argument("--exclude-before", type=int, default=20)
    args = parser.parse_args()
    manifest = args.manifest or args.output.with_suffix(".manifest.json")
    write_subset(args.source, args.output, manifest, args.count, args.exclude_before)
    print(f"wrote {args.count} tasks to {args.output}")
    print(f"manifest: {manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
