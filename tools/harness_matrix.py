"""Run the same versioned task suite through external harness adapters.

The runner deliberately knows nothing about vendor-specific CLIs.  Every adapter
is a command that accepts one JSON request on stdin and emits one JSON response
on stdout.  This makes OpenClaw, Hermes, Pi and Nanobot comparable without
pretending their command-line interfaces are interchangeable.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import subprocess
import sys
import time
from typing import Any, Mapping, Sequence


ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = "llamacode-harness-v1"


class MatrixError(RuntimeError):
    """A configuration, protocol, or adapter execution error."""


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise MatrixError(f"cannot read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise MatrixError(f"JSON root must be an object: {path}")
    return value


def load_suite(path: Path) -> dict[str, Any]:
    source = load_json(path)
    raw_tasks = source.get("prompts", source.get("tasks"))
    if not isinstance(raw_tasks, list) or not raw_tasks:
        raise MatrixError("suite must contain a non-empty prompts/tasks list")
    tasks: list[dict[str, Any]] = []
    seen: set[str] = set()
    for index, raw in enumerate(raw_tasks, 1):
        if not isinstance(raw, dict):
            raise MatrixError(f"suite task {index} is not an object")
        task_id = str(raw.get("id", f"task-{index:03d}")).strip()
        prompt = str(raw.get("prompt", ""))
        if not task_id or not prompt.strip():
            raise MatrixError(f"suite task {index} needs id and prompt")
        if task_id in seen:
            raise MatrixError(f"duplicate suite task id: {task_id}")
        seen.add(task_id)
        acceptance = raw.get("acceptance", {})
        if acceptance is None:
            acceptance = {}
        if not isinstance(acceptance, dict):
            raise MatrixError(f"task {task_id} acceptance must be an object")
        tasks.append({
            "id": task_id,
            "category": str(raw.get("category", "other")),
            "prompt": prompt,
            "maxTokens": int(raw.get("maxTokens", 8192)),
            "acceptance": acceptance,
        })
    return {
        "id": str(source.get("id", path.stem)),
        "name": str(source.get("name", path.stem)),
        "description": str(source.get("description", "")),
        "tasks": tasks,
    }


def load_adapters(path: Path, selected: Sequence[str] | None = None) -> list[dict[str, Any]]:
    source = load_json(path)
    raw_adapters = source.get("adapters", [])
    if isinstance(raw_adapters, dict):
        raw_adapters = [dict(value, id=key) for key, value in raw_adapters.items()]
    if not isinstance(raw_adapters, list) or not raw_adapters:
        raise MatrixError("adapter manifest must contain a non-empty adapters list")
    wanted = {item.strip() for item in selected or [] if item.strip()}
    adapters: list[dict[str, Any]] = []
    seen: set[str] = set()
    for index, raw in enumerate(raw_adapters, 1):
        if not isinstance(raw, dict):
            raise MatrixError(f"adapter {index} is not an object")
        adapter_id = str(raw.get("id", "")).strip()
        if not adapter_id:
            raise MatrixError(f"adapter {index} has no id")
        if adapter_id in seen:
            raise MatrixError(f"duplicate adapter id: {adapter_id}")
        seen.add(adapter_id)
        if wanted and adapter_id not in wanted:
            continue
        if raw.get("enabled", True) is False:
            raise MatrixError(f"adapter is disabled: {adapter_id}")
        adapters.append(dict(raw, id=adapter_id))
    if wanted and {item["id"] for item in adapters} != wanted:
        missing = sorted(wanted - {item["id"] for item in adapters})
        raise MatrixError("unknown adapter(s): " + ", ".join(missing))
    if not adapters:
        raise MatrixError("no adapters selected")
    return adapters


def adapter_command(adapter: Mapping[str, Any]) -> list[str]:
    command = adapter.get("command")
    if not command:
        env_name = str(adapter.get("commandEnv", "")).strip()
        if not env_name:
            raise MatrixError(f"adapter {adapter['id']} needs command or commandEnv")
        raw = os.environ.get(env_name, "").strip()
        if not raw:
            raise MatrixError(
                f"adapter {adapter['id']} is not configured; set {env_name} to a JSON command array"
            )
        try:
            command = json.loads(raw)
        except json.JSONDecodeError as exc:
            raise MatrixError(f"{env_name} must contain a JSON command array: {exc}") from exc
    if (not isinstance(command, list) or not command
            or not all(isinstance(item, str) and item for item in command)):
        raise MatrixError(f"adapter {adapter['id']} command must be a non-empty string array")
    return list(command)


def adapter_cwd(adapter: Mapping[str, Any]) -> Path:
    raw = str(adapter.get("cwd", "")).strip()
    path = (ROOT / raw).resolve() if raw and not Path(raw).is_absolute() else Path(raw or ROOT).resolve()
    if not path.is_dir():
        raise MatrixError(f"adapter {adapter['id']} cwd does not exist: {path}")
    return path


def canonical_arguments(raw: Any) -> tuple[str, bool]:
    if raw is None:
        return "{}", True
    value = raw
    if isinstance(raw, str):
        if not raw.strip():
            return "{}", True
        try:
            value = json.loads(raw)
        except json.JSONDecodeError:
            return "<invalid-json>", False
    if not isinstance(value, dict):
        return "<invalid-object>", False
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False), True


def tool_call_score(calls: Any, expected: Any) -> dict[str, Any]:
    actual_calls = calls if isinstance(calls, list) else []
    expected_calls = expected if isinstance(expected, list) else []
    normalized: list[dict[str, Any]] = []
    seen: set[str] = set()
    successful = failed = incomplete = invalid = redundant = 0
    for raw in actual_calls:
        call = raw if isinstance(raw, dict) else {}
        tool = str(call.get("tool", "")).strip()
        args = call["arguments"] if "arguments" in call else call.get("args")
        args_json, args_valid = canonical_arguments(args)
        valid = bool(tool) and args_valid
        invalid += not valid
        signature = f"{tool}\n{args_json}"
        redundant += signature in seen
        seen.add(signature)
        completed = bool(call["completed"]) if "completed" in call else "ok" in call
        if not completed:
            incomplete += 1
        elif bool(call.get("ok", False)):
            successful += 1
        else:
            failed += 1
        normalized.append({
            "tool": tool,
            "arguments": args_json,
            "valid": valid,
            "completed": completed,
            "ok": completed and bool(call.get("ok", False)),
        })

    def matches(actual: Mapping[str, Any], wanted: Mapping[str, Any]) -> bool:
        if not actual.get("valid") or actual.get("tool") != str(wanted.get("tool", "")).strip():
            return False
        if "arguments" not in wanted and "args" not in wanted:
            return True
        args = wanted["arguments"] if "arguments" in wanted else wanted.get("args")
        wanted_json, wanted_valid = canonical_arguments(args)
        return wanted_valid and actual.get("arguments") == wanted_json

    matched = 0
    unexpected = 0
    matched_indices: set[int] = set()
    for actual in normalized:
        found = None
        for index, wanted in enumerate(expected_calls):
            if index in matched_indices or not isinstance(wanted, dict):
                continue
            if matches(actual, wanted):
                found = index
                break
        if found is None:
            unexpected += 1
        else:
            matched_indices.add(found)
            matched += 1

    expected_count = len(expected_calls)
    total = len(actual_calls)
    has_expectations = expected_count > 0
    missing = expected_count - matched if has_expectations else 0
    precision = matched / total * 100.0 if has_expectations and total else None
    recall = matched / expected_count * 100.0 if has_expectations else None
    f1 = (2 * precision * recall / (precision + recall)
          if precision is not None and recall is not None and precision + recall else None)
    sequence_exact = has_expectations and total == expected_count
    if sequence_exact:
        sequence_exact = all(
            actual["completed"] and actual["ok"] and isinstance(wanted, dict)
            and matches(actual, wanted)
            for actual, wanted in zip(normalized, expected_calls)
        )
    return {
        "totalCalls": total,
        "successfulCalls": successful,
        "failedCalls": failed,
        "incompleteCalls": incomplete,
        "invalidCalls": invalid,
        "redundantCalls": redundant,
        "successRatePct": successful / total * 100.0 if total else None,
        "expectedCalls": expected_count,
        "matchedExpectedCalls": matched,
        "missingExpectedCalls": missing,
        "unexpectedCalls": unexpected if has_expectations else 0,
        "precisionPct": precision,
        "recallPct": recall,
        "f1Pct": f1,
        "sequenceExact": sequence_exact,
    }


def plan_invocations(adapters: Sequence[Mapping[str, Any]], tasks: Sequence[Mapping[str, Any]],
                     passes: int, seed: int) -> list[dict[str, Any]]:
    rng = random.Random(seed)
    planned: list[dict[str, Any]] = []
    for pass_number in range(1, passes + 1):
        batch = [(str(adapter["id"]), dict(task)) for adapter in adapters for task in tasks]
        rng.shuffle(batch)
        for order, (adapter_id, task) in enumerate(batch):
            planned.append({
                "pass": pass_number,
                "order": order,
                "adapterId": adapter_id,
                "task": task,
            })
    return planned


def invoke(adapter: Mapping[str, Any], request: Mapping[str, Any], timeout: float) -> tuple[dict[str, Any], float]:
    command = adapter_command(adapter)
    cwd = adapter_cwd(adapter)
    env = os.environ.copy()
    configured_env = adapter.get("env", {})
    if configured_env:
        if not isinstance(configured_env, dict):
            raise MatrixError(f"adapter {adapter['id']} env must be an object")
        env.update({str(key): str(value) for key, value in configured_env.items()})
    env["LLAMACODE_HARNESS_PROTOCOL"] = PROTOCOL
    started = time.perf_counter()
    try:
        completed = subprocess.run(
            command,
            cwd=cwd,
            input=json.dumps(request, ensure_ascii=False, separators=(",", ":")) + "\n",
            text=True,
            capture_output=True,
            timeout=timeout,
            check=False,
            env=env,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise MatrixError(f"adapter {adapter['id']} execution failed: {exc}") from exc
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    if completed.returncode:
        detail = completed.stderr.strip()[-1000:]
        raise MatrixError(f"adapter {adapter['id']} exited {completed.returncode}: {detail}")
    lines = [line for line in completed.stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise MatrixError(f"adapter {adapter['id']} must emit exactly one JSON line on stdout")
    try:
        response = json.loads(lines[0])
    except json.JSONDecodeError as exc:
        raise MatrixError(f"adapter {adapter['id']} emitted invalid JSON: {exc}") from exc
    if not isinstance(response, dict):
        raise MatrixError(f"adapter {adapter['id']} response must be an object")
    if response.get("protocol") != PROTOCOL:
        raise MatrixError(f"adapter {adapter['id']} returned wrong protocol")
    if response.get("requestId") != request.get("requestId"):
        raise MatrixError(f"adapter {adapter['id']} returned wrong requestId")
    return response, elapsed_ms


def median(values: Sequence[float]) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    middle = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[middle]
    return (ordered[middle - 1] + ordered[middle]) / 2.0


def aggregate(rows: Sequence[Mapping[str, Any]], adapter_id: str) -> dict[str, Any]:
    own = [row for row in rows if row.get("adapterId") == adapter_id]
    completed = [row for row in own if row.get("invocationOk")]
    passed = [row for row in own if row.get("passed")]
    elapsed = [float(row["elapsedMs"]) for row in completed]
    tool_quality = [row["toolCallQuality"] for row in completed
                    if isinstance(row.get("toolCallQuality"), dict)]

    def known(key: str) -> list[float]:
        return [float(item[key]) for item in tool_quality
                if isinstance(item.get(key), (int, float)) and item[key] is not None]

    return {
        "adapterId": adapter_id,
        "runs": len(own),
        "invocationOkRuns": len(completed),
        "passedRuns": len(passed),
        "failedRuns": len(own) - len(passed),
        "invocationSuccessRatePct": len(completed) / len(own) * 100.0 if own else 0.0,
        "taskSuccessRatePct": len(passed) / len(own) * 100.0 if own else 0.0,
        "medianElapsedMs": median(elapsed),
        "meanElapsedMs": sum(elapsed) / len(elapsed) if elapsed else None,
        "medianToolF1Pct": median(known("f1Pct")),
        "medianToolSuccessRatePct": median(known("successRatePct")),
        "medianToolRedundantCalls": median(known("redundantCalls")),
        "medianToolCalls": median(known("totalCalls")),
    }


def compare(rows: Sequence[Mapping[str, Any]], baseline: str, candidate: str) -> dict[str, Any]:
    key = lambda row: (int(row["pass"]), str(row["taskId"]))
    base_rows = {key(row): row for row in rows if row.get("adapterId") == baseline}
    cand_rows = {key(row): row for row in rows if row.get("adapterId") == candidate}
    shared = sorted(set(base_rows) & set(cand_rows))
    if not shared:
        return {"baselineAdapterId": baseline, "candidateAdapterId": candidate,
                "pairedTasks": 0, "balanced": False}

    base = [base_rows[item] for item in shared]
    cand = [cand_rows[item] for item in shared]
    base_elapsed = [float(row["elapsedMs"]) for row in base if row.get("invocationOk")]
    cand_elapsed = [float(row["elapsedMs"]) for row in cand if row.get("invocationOk")]
    base_f1 = [row["toolCallQuality"].get("f1Pct") for row in base
               if row.get("toolCallQuality", {}).get("f1Pct") is not None]
    cand_f1 = [row["toolCallQuality"].get("f1Pct") for row in cand
               if row.get("toolCallQuality", {}).get("f1Pct") is not None]
    base_redundant = [row["toolCallQuality"].get("redundantCalls") for row in base]
    cand_redundant = [row["toolCallQuality"].get("redundantCalls") for row in cand]
    base_time = median(base_elapsed)
    cand_time = median(cand_elapsed)
    base_f1_median = median([float(value) for value in base_f1])
    cand_f1_median = median([float(value) for value in cand_f1])
    return {
        "baselineAdapterId": baseline,
        "candidateAdapterId": candidate,
        "pairedTasks": len(shared),
        "balanced": len(base) == len(cand),
        "taskSuccessDeltaPctPoints": (
            sum(bool(row.get("passed")) for row in cand) / len(cand) * 100.0
            - sum(bool(row.get("passed")) for row in base) / len(base) * 100.0),
        "elapsedChangePct": ((cand_time / base_time) - 1.0) * 100.0
        if base_time and cand_time else None,
        "toolF1DeltaPctPoints": cand_f1_median - base_f1_median
        if cand_f1_median is not None and base_f1_median is not None else None,
        "toolRedundantCallsDelta": median([float(value) for value in cand_redundant])
        - median([float(value) for value in base_redundant]),
    }


def run_matrix(suite: Mapping[str, Any], adapters: Sequence[Mapping[str, Any]],
               passes: int, seed: int, timeout: float) -> dict[str, Any]:
    tasks = suite["tasks"]
    planned = plan_invocations(adapters, tasks, passes, seed)
    rows: list[dict[str, Any]] = []
    errors: list[str] = []
    adapter_by_id = {str(adapter["id"]): adapter for adapter in adapters}
    for item in planned:
        task = item["task"]
        adapter_id = item["adapterId"]
        request_id = f"{seed}-{item['pass']}-{item['order']}-{adapter_id}-{task['id']}"
        request = {
            "protocol": PROTOCOL,
            "requestId": request_id,
            "suite": {"id": suite["id"], "name": suite["name"]},
            "pass": item["pass"],
            "task": task,
        }
        row: dict[str, Any] = {
            "pass": item["pass"],
            "order": item["order"],
            "adapterId": adapter_id,
            "taskId": task["id"],
            "category": task["category"],
            "invocationOk": False,
            "passed": False,
            "elapsedMs": None,
            "toolCallQuality": tool_call_score([], task["acceptance"].get("toolCalls", [])),
        }
        try:
            response, elapsed_ms = invoke(adapter_by_id[adapter_id], request, timeout)
            reported_elapsed = response.get("elapsedMs", elapsed_ms)
            try:
                reported_elapsed = float(reported_elapsed)
            except (TypeError, ValueError) as exc:
                raise MatrixError(
                    f"adapter {adapter_id} returned invalid elapsedMs") from exc
            if not math.isfinite(reported_elapsed) or reported_elapsed < 0.0:
                raise MatrixError(
                    f"adapter {adapter_id} returned invalid elapsedMs")
            row.update({
                "invocationOk": bool(response.get("ok", True)),
                "passed": bool(response.get("passed", response.get("ok", False))),
                "elapsedMs": reported_elapsed,
                "adapterElapsedMs": elapsed_ms,
                "toolCalls": response.get("toolCalls", []),
                "toolCallQuality": tool_call_score(
                    response.get("toolCalls", []), task["acceptance"].get("toolCalls", [])),
            })
            for key in ("qualityScore", "qualityTotal", "response", "error"):
                if key in response:
                    row[key] = response[key]
        except MatrixError as exc:
            error = str(exc)
            row["error"] = error
            errors.append(f"{adapter_id}/{task['id']}: {error}")
        rows.append(row)

    adapter_ids = [str(adapter["id"]) for adapter in adapters]
    summaries = [aggregate(rows, adapter_id) for adapter_id in adapter_ids]
    comparisons = [compare(rows, adapter_ids[0], adapter_id)
                   for adapter_id in adapter_ids[1:]]
    order_by_pass = []
    for pass_number in range(1, passes + 1):
        order_by_pass.append([
            {"adapterId": item["adapterId"], "taskId": item["task"]["id"]}
            for item in planned if item["pass"] == pass_number
        ])
    return {
        "schemaVersion": 1,
        "protocol": PROTOCOL,
        "suiteId": suite["id"],
        "suiteName": suite["name"],
        "passes": passes,
        "seed": seed,
        "adapterIds": adapter_ids,
        "taskCount": len(tasks),
        "invocationCount": len(rows),
        "suiteTaskIds": [task["id"] for task in tasks],
        "orderByPass": order_by_pass,
        "adapters": summaries,
        "comparisons": comparisons,
        "errors": errors,
        "rows": rows,
    }


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--adapters", default="", help="comma-separated adapter ids")
    parser.add_argument("--passes", type=int, default=5)
    parser.add_argument("--seed", type=int, default=4242)
    parser.add_argument("--timeout-sec", type=float, default=900.0)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        if args.passes < 1:
            raise MatrixError("--passes must be >= 1")
        if args.timeout_sec <= 0:
            raise MatrixError("--timeout-sec must be > 0")
        suite_path = args.suite if args.suite.is_absolute() else ROOT / args.suite
        manifest_path = args.manifest if args.manifest.is_absolute() else ROOT / args.manifest
        suite = load_suite(suite_path.resolve())
        selected = [item for item in args.adapters.split(",") if item.strip()]
        adapters = load_adapters(manifest_path.resolve(), selected)
        report = run_matrix(suite, adapters, args.passes, args.seed, args.timeout_sec)
        report["suiteSha256"] = hashlib.sha256(suite_path.read_bytes()).hexdigest()
        out_path = args.out if args.out.is_absolute() else Path.cwd() / args.out
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        if report["errors"]:
            print(f"matrix completed with {len(report['errors'])} adapter error(s)", file=sys.stderr)
            return 1
        print(f"matrix OK: {len(adapters)} adapters x {len(suite['tasks'])} tasks x {args.passes} passes")
        print(f"report: {out_path}")
        return 0
    except MatrixError as exc:
        print(f"matrix error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
