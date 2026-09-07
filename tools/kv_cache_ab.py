"""Reproducible A/B runner for llama-server KV-cache configurations.

The runner deliberately stays outside the application runtime. It starts one
server per variant, waits for /health, runs the compiled qa_kv_cache probe and
then compares the same deterministic NIAH cases. A JSON config keeps arbitrary
llama-server flags and environment variables lossless on both Windows and
Linux, which is important for sidecar-based runtimes. Each variant may optionally
override ``serverExe`` when an A/B comparison needs different runtime builds.
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import os
from pathlib import Path, PurePosixPath
import shlex
import statistics
import subprocess
import sys
import time
from typing import Any, Callable, Dict, Iterable, List, Mapping, Optional, Sequence, Tuple
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


SCHEMA = "llamacode-kv-cache-ab-v1"
SERVER_VALUE_FLAGS = {
    "--model", "-m", "--host", "--port", "-H",
}


def _as_string_list(value: Any, field: str) -> List[str]:
    if value is None:
        return []
    if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
        raise ValueError(f"{field} debe ser una lista de strings")
    return list(value)


def normalize_server_args(
    args: Sequence[str], model_path: str, host: str, port: int,
    add_metrics: bool = True,
) -> List[str]:
    """Remove server-owned flags and append one canonical endpoint.

    Keeping this pure makes it testable without starting a process. The model,
    host and port supplied by the runner always win over values copied into a
    profile, preventing an A/B run from silently talking to another server.
    """
    out: List[str] = []
    index = 0
    while index < len(args):
        token = args[index]
        if token in SERVER_VALUE_FLAGS:
            index += 2
            continue
        # Also handle --port=1234 and --model=foo forms.
        if any(token.startswith(flag + "=") for flag in SERVER_VALUE_FLAGS):
            index += 1
            continue
        out.append(token)
        index += 1

    if add_metrics and "--metrics" not in out:
        out.append("--metrics")
    out.extend(["--model", model_path, "--host", host, "--port", str(port)])
    return out


def _row_key(row: Mapping[str, Any], pass_number: int) -> Tuple[int, str, str, int, float]:
    try:
        context = int(row.get("contextTokens", -1))
    except (TypeError, ValueError):
        context = -1
    try:
        depth = round(float(row.get("depth", -1.0)), 8)
    except (TypeError, ValueError):
        depth = -1.0
    return pass_number, str(row.get("stream", "")), str(row.get("id", "")), context, depth


def _finite_positive(value: Any) -> Optional[float]:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) and number > 0.0 else None


def _delta(candidate: Any, baseline: Any) -> Optional[float]:
    candidate_value = _finite_positive(candidate)
    baseline_value = _finite_positive(baseline)
    if candidate_value is None or baseline_value is None:
        return None
    return (candidate_value - baseline_value) * 100.0 / baseline_value


def summarize_deltas(values: Iterable[float], higher_is_better: bool = True) -> Dict[str, Any]:
    samples = [float(value) for value in values if math.isfinite(float(value))]
    if not samples:
        return {
            "count": 0, "meanPct": -1.0, "medianPct": -1.0,
            "ci95LowPct": -1.0, "ci95HighPct": -1.0,
            "significant": False, "winner": "insufficient-data",
        }

    mean = statistics.fmean(samples)
    median = statistics.median(samples)
    if len(samples) < 2:
        low = high = -1.0
        significant = False
        winner = "insufficient-data"
    else:
        margin = 1.96 * statistics.stdev(samples) / math.sqrt(len(samples))
        low, high = mean - margin, mean + margin
        significant = low > 0.0 or high < 0.0
        if not significant:
            winner = "within-noise"
        elif higher_is_better:
            winner = "candidate" if mean > 0.0 else "baseline"
        else:
            winner = "candidate" if mean < 0.0 else "baseline"

    return {
        "count": len(samples), "meanPct": mean, "medianPct": median,
        "ci95LowPct": low, "ci95HighPct": high,
        "significant": significant, "winner": winner,
    }


def _receipt_rows(receipt: Mapping[str, Any], pass_number: int) -> Dict[Tuple[int, str, str, int, float], Dict[str, Any]]:
    rows: Dict[Tuple[int, str, str, int, float], Dict[str, Any]] = {}
    for original in receipt.get("results", []) or []:
        if not isinstance(original, dict):
            continue
        row = copy.deepcopy(original)
        row["pass"] = pass_number
        rows[_row_key(row, pass_number)] = row
    return rows


def compare_receipts(
    baseline_runs: Sequence[Mapping[str, Any]],
    candidate_runs: Sequence[Mapping[str, Any]],
) -> Dict[str, Any]:
    """Pair deterministic rows and calculate candidate-vs-baseline deltas."""
    baseline: Dict[Tuple[int, str, str, float], Dict[str, Any]] = {}
    candidate: Dict[Tuple[int, str, str, float], Dict[str, Any]] = {}
    for pass_number, receipt in enumerate(baseline_runs, 1):
        baseline.update(_receipt_rows(receipt, pass_number))
    for pass_number, receipt in enumerate(candidate_runs, 1):
        candidate.update(_receipt_rows(receipt, pass_number))

    paired: List[Dict[str, Any]] = []
    unmatched_baseline: List[Dict[str, Any]] = []
    unmatched_candidate: List[Dict[str, Any]] = []
    for key in sorted(set(baseline) | set(candidate), key=str):
        left = baseline.get(key)
        right = candidate.get(key)
        if left is None:
            unmatched_candidate.append(right or {})
            continue
        if right is None:
            unmatched_baseline.append(left)
            continue
        paired.append({
            "pass": key[0],
            "stream": key[1],
            "id": key[2],
            "contextTokens": left.get("contextTokens"),
            "depth": left.get("depth"),
            "baselineExact": bool(left.get("exactPasskey")),
            "candidateExact": bool(right.get("exactPasskey")),
            "baselineContains": bool(left.get("containsPasskey")),
            "candidateContains": bool(right.get("containsPasskey")),
            "baselineLatencyMs": left.get("latencyMs"),
            "candidateLatencyMs": right.get("latencyMs"),
            "baselinePromptTps": left.get("promptTps"),
            "candidatePromptTps": right.get("promptTps"),
            "baselineGenTps": left.get("genTps"),
            "candidateGenTps": right.get("genTps"),
            "latencyDeltaPct": _delta(right.get("latencyMs"), left.get("latencyMs")),
            "promptTpsDeltaPct": _delta(right.get("promptTps"), left.get("promptTps")),
            "genTpsDeltaPct": _delta(right.get("genTps"), left.get("genTps")),
        })

    def deltas(name: str) -> List[float]:
        return [float(row[name]) for row in paired if row.get(name) is not None]

    baseline_exact = sum(1 for row in paired if row["baselineExact"])
    candidate_exact = sum(1 for row in paired if row["candidateExact"])
    return {
        "pairCount": len(paired),
        "baselineRowCount": len(baseline),
        "candidateRowCount": len(candidate),
        "unmatchedBaseline": unmatched_baseline,
        "unmatchedCandidate": unmatched_candidate,
        "pairedRows": paired,
        "baselineExactPasses": baseline_exact,
        "candidateExactPasses": candidate_exact,
        "exactPassDelta": candidate_exact - baseline_exact,
        "latency": summarize_deltas(deltas("latencyDeltaPct"), higher_is_better=False),
        "promptTps": summarize_deltas(deltas("promptTpsDeltaPct"), higher_is_better=True),
        "genTps": summarize_deltas(deltas("genTpsDeltaPct"), higher_is_better=True),
    }


def compare_run_records(
    baseline_runs: Sequence[Mapping[str, Any]],
    candidate_runs: Sequence[Mapping[str, Any]],
) -> Dict[str, Any]:
    """Compare records while preserving original pass numbers after failures."""
    baseline_receipts = [
        (int(record.get("pass", index)), record["receipt"])
        for index, record in enumerate(baseline_runs, 1)
        if isinstance(record.get("receipt"), dict)
    ]
    candidate_receipts = [
        (int(record.get("pass", index)), record["receipt"])
        for index, record in enumerate(candidate_runs, 1)
        if isinstance(record.get("receipt"), dict)
    ]
    baseline: Dict[Tuple[int, str, str, int, float], Dict[str, Any]] = {}
    candidate: Dict[Tuple[int, str, str, int, float], Dict[str, Any]] = {}
    for pass_number, receipt in baseline_receipts:
        baseline.update(_receipt_rows(receipt, pass_number))
    for pass_number, receipt in candidate_receipts:
        candidate.update(_receipt_rows(receipt, pass_number))

    # Reuse the public receipt comparison implementation after restoring the
    # original pass order. This path is only needed when a server failed in an
    # intermediate pass; the normal path remains the compact receipt API.
    baseline_ordered = [receipt for _, receipt in sorted(baseline_receipts)]
    candidate_ordered = [receipt for _, receipt in sorted(candidate_receipts)]
    if [number for number, _ in baseline_receipts] == list(range(1, len(baseline_receipts) + 1)) \
            and [number for number, _ in candidate_receipts] == list(range(1, len(candidate_receipts) + 1)):
        return compare_receipts(baseline_ordered, candidate_ordered)

    # Same calculation as compare_receipts, with explicit pass-aware maps.
    paired: List[Dict[str, Any]] = []
    unmatched_baseline: List[Dict[str, Any]] = []
    unmatched_candidate: List[Dict[str, Any]] = []
    for key in sorted(set(baseline) | set(candidate), key=str):
        left = baseline.get(key)
        right = candidate.get(key)
        if left is None:
            unmatched_candidate.append(right or {})
            continue
        if right is None:
            unmatched_baseline.append(left)
            continue
        paired.append({
            "pass": key[0], "stream": key[1], "id": key[2],
            "contextTokens": left.get("contextTokens"), "depth": left.get("depth"),
            "baselineExact": bool(left.get("exactPasskey")),
            "candidateExact": bool(right.get("exactPasskey")),
            "baselineContains": bool(left.get("containsPasskey")),
            "candidateContains": bool(right.get("containsPasskey")),
            "baselineLatencyMs": left.get("latencyMs"),
            "candidateLatencyMs": right.get("latencyMs"),
            "baselinePromptTps": left.get("promptTps"),
            "candidatePromptTps": right.get("promptTps"),
            "baselineGenTps": left.get("genTps"),
            "candidateGenTps": right.get("genTps"),
            "latencyDeltaPct": _delta(right.get("latencyMs"), left.get("latencyMs")),
            "promptTpsDeltaPct": _delta(right.get("promptTps"), left.get("promptTps")),
            "genTpsDeltaPct": _delta(right.get("genTps"), left.get("genTps")),
        })

    def deltas(name: str) -> List[float]:
        return [float(row[name]) for row in paired if row.get(name) is not None]

    baseline_exact = sum(1 for row in paired if row["baselineExact"])
    candidate_exact = sum(1 for row in paired if row["candidateExact"])
    return {
        "pairCount": len(paired), "baselineRowCount": len(baseline),
        "candidateRowCount": len(candidate), "unmatchedBaseline": unmatched_baseline,
        "unmatchedCandidate": unmatched_candidate, "pairedRows": paired,
        "baselineExactPasses": baseline_exact, "candidateExactPasses": candidate_exact,
        "exactPassDelta": candidate_exact - baseline_exact,
        "latency": summarize_deltas(deltas("latencyDeltaPct"), higher_is_better=False),
        "promptTps": summarize_deltas(deltas("promptTpsDeltaPct"), higher_is_better=True),
        "genTps": summarize_deltas(deltas("genTpsDeltaPct"), higher_is_better=True),
    }


def validate_config(config: Mapping[str, Any]) -> Dict[str, Any]:
    server = str(config.get("serverExe", "")).strip()
    model = str(config.get("modelPath", "")).strip()
    if not server:
        raise ValueError("Falta serverExe en la configuración")
    if not model:
        raise ValueError("Falta modelPath en la configuración")
    common = _as_string_list(config.get("commonArgs", []), "commonArgs")
    launcher_value = config.get("launcher", {})
    if launcher_value is None:
        launcher_value = {}
    if not isinstance(launcher_value, dict):
        raise ValueError("launcher debe ser un objeto")
    launcher_kind = str(launcher_value.get("kind", "native")).strip().lower()
    if launcher_kind not in {"native", "wsl"}:
        raise ValueError("launcher.kind debe ser native o wsl")
    launcher: Dict[str, str] = {"kind": launcher_kind}
    if launcher_kind == "wsl":
        distro = str(launcher_value.get("distro", "")).strip()
        if not distro:
            raise ValueError("launcher.distro es obligatorio para WSL")
        if not PurePosixPath(server).is_absolute():
            raise ValueError("serverExe debe ser una ruta absoluta Linux para WSL")
        if not PurePosixPath(model).is_absolute():
            raise ValueError("modelPath debe ser una ruta absoluta Linux para WSL")
        launcher["distro"] = distro
        cwd = str(launcher_value.get("cwd", "")).strip()
        if cwd:
            if not PurePosixPath(cwd).is_absolute():
                raise ValueError("launcher.cwd debe ser una ruta absoluta Linux")
            launcher["cwd"] = cwd
    variants = config.get("variants")
    if not isinstance(variants, list) or len(variants) != 2:
        raise ValueError("variants debe contener exactamente baseline y candidate")

    normalized: Dict[str, Any] = {
        "serverExe": server, "modelPath": model, "commonArgs": common,
        "launcher": launcher,
        "variants": [],
    }
    seen = set()
    for index, variant in enumerate(variants):
        if not isinstance(variant, dict):
            raise ValueError(f"variants[{index}] debe ser un objeto")
        variant_id = str(variant.get("id", "")).strip()
        if not variant_id:
            raise ValueError(f"variants[{index}] no tiene id")
        if variant_id in seen:
            raise ValueError(f"id de variante duplicado: {variant_id}")
        seen.add(variant_id)
        variant_server = str(variant.get("serverExe", server)).strip()
        if not variant_server:
            raise ValueError(f"variants[{index}].serverExe no puede estar vacio")
        if launcher_kind == "wsl" and not PurePosixPath(variant_server).is_absolute():
            raise ValueError(f"variants[{index}].serverExe debe ser una ruta absoluta Linux para WSL")
        args = _as_string_list(variant.get("args", []), f"variants[{index}].args")
        environment = variant.get("env", {})
        if not isinstance(environment, dict) or any(
            not isinstance(key, str) or not isinstance(value, str)
            for key, value in environment.items()
        ):
            raise ValueError(f"variants[{index}].env debe ser un objeto string -> string")
        normalized["variants"].append({
            "id": variant_id, "serverExe": variant_server,
            "args": args, "env": dict(environment),
        })
    return normalized


def _join_url(host: str, port: int) -> str:
    return f"http://{host}:{port}"


def _log_tail(path: Path, max_bytes: int = 4000) -> str:
    """Return a bounded UTF-8 tail for actionable startup diagnostics."""
    try:
        data = path.read_bytes()
    except OSError:
        return ""
    return data[-max_bytes:].decode("utf-8", errors="replace").strip()


def wait_for_server(base_url: str, timeout_seconds: float,
                    process: Optional[subprocess.Popen] = None,
                    poll_seconds: float = 0.5) -> None:
    deadline = time.monotonic() + timeout_seconds
    last_error = "sin respuesta"
    while time.monotonic() < deadline:
        if process is not None:
            exit_code = process.poll()
            if exit_code is not None:
                raise RuntimeError(
                    f"llama-server terminó antes de /health (exit={exit_code})")
        try:
            request = Request(base_url + "/health", method="GET")
            with urlopen(request, timeout=min(3.0, max(0.5, timeout_seconds))) as response:
                if response.status == 200:
                    return
                last_error = f"HTTP {response.status}"
        except HTTPError as error:
            last_error = f"HTTP {error.code}"
        except (URLError, OSError, TimeoutError) as error:
            last_error = str(error)
        time.sleep(poll_seconds)
    raise RuntimeError(f"llama-server no quedó listo en {base_url}: {last_error}")


class RunningServer:
    def __init__(self, process: subprocess.Popen, log_handle: Any, log_path: Path,
                 stop_callback: Optional[Callable[[], None]] = None):
        self.process = process
        self.log_handle = log_handle
        self.log_path = log_path
        self.stop_callback = stop_callback

    def stop(self, timeout_seconds: float = 15.0) -> int:
        if self.process.poll() is None:
            if self.stop_callback is not None:
                try:
                    self.stop_callback()
                except (OSError, subprocess.SubprocessError):
                    pass
            self.process.terminate()
            try:
                self.process.wait(timeout=timeout_seconds)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5.0)
        code = self.process.returncode
        self.log_handle.close()
        return int(code if code is not None else -1)


def start_server(
    server_exe: Any, args: Sequence[str], env_overrides: Mapping[str, str],
    log_path: Path, launcher: Optional[Mapping[str, str]] = None,
) -> RunningServer:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    handle = log_path.open("wb")
    launcher = launcher or {"kind": "native"}
    if launcher.get("kind", "native") == "wsl":
        distro = launcher["distro"]
        cwd = launcher.get("cwd") or str(PurePosixPath(str(server_exe)).parent)
        pid_file = f"/tmp/llamacode-kv-cache-ab-{os.getpid()}-{args[-1]}.pid"
        assignments = [f"{key}={shlex.quote(value)}"
                       for key, value in sorted(env_overrides.items())]
        quoted_args = " ".join(shlex.quote(str(value)) for value in args)
        command_text = (
            f"cd -- {shlex.quote(cwd)} && "
            f"echo $$ > {shlex.quote(pid_file)} && "
            f"exec env {' '.join(assignments)} {shlex.quote(str(server_exe))} {quoted_args}"
        )
        command = ["wsl.exe", "-d", distro, "--", "bash", "-lc", command_text]

        def stop_wsl() -> None:
            stop_text = (
                f"if test -s {shlex.quote(pid_file)}; then "
                f"kill -TERM \"$(cat {shlex.quote(pid_file)})\" 2>/dev/null || true; fi; "
                f"rm -f {shlex.quote(pid_file)}"
            )
            subprocess.run(
                ["wsl.exe", "-d", distro, "--", "bash", "-lc", stop_text],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                check=False, timeout=10.0,
            )

        try:
            process = subprocess.Popen(
                command, stdout=handle, stderr=subprocess.STDOUT,
            )
        except Exception:
            handle.close()
            raise
        return RunningServer(process, handle, log_path, stop_wsl)

    environment = os.environ.copy()
    environment.update(env_overrides)
    try:
        process = subprocess.Popen(
            [str(server_exe), *args],
            stdout=handle,
            stderr=subprocess.STDOUT,
            env=environment,
            cwd=str(server_exe.parent),
        )
    except Exception:
        handle.close()
        raise
    return RunningServer(process, handle, log_path)


def _wsl_test_file(distro: str, path: str) -> None:
    try:
        completed = subprocess.run(
            ["wsl.exe", "-d", distro, "--", "test", "-f", path],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            check=False, timeout=15.0,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise ValueError(f"no se pudo consultar WSL ({distro}): {error}") from error
    if completed.returncode != 0:
        raise ValueError(f"ruta WSL no existe o no es archivo: {path}")


def run_probe(
    probe: Path, base_url: str, contexts: str, depths: str, users: int,
    n_predict: int, timeout_ms: int, process_timeout: float,
) -> Tuple[Dict[str, Any], int, str]:
    command = [
        str(probe), base_url, "--contexts", contexts, "--depths", depths,
        "--users", str(users), "--n-predict", str(n_predict),
        "--timeout-ms", str(timeout_ms),
    ]
    completed = subprocess.run(
        command, capture_output=True, text=True, encoding="utf-8", errors="replace",
        timeout=process_timeout,
    )
    stdout = completed.stdout.strip()
    try:
        receipt = json.loads(stdout)
    except json.JSONDecodeError as error:
        detail = (completed.stderr.strip() or stdout[-2000:])
        raise RuntimeError(f"qa_kv_cache no devolvió JSON: {error}; {detail}") from error
    if not isinstance(receipt, dict):
        raise RuntimeError("qa_kv_cache devolvió JSON que no es un objeto")
    return receipt, int(completed.returncode), completed.stderr.strip()


def _default_probe() -> Path:
    root = Path(__file__).resolve().parents[1]
    candidates = [root / "build" / "Debug" / "qa_kv_cache.exe",
                  root / "build" / "Debug" / "qa_kv_cache"]
    return next((path for path in candidates if path.exists()), candidates[0])


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, help="JSON con serverExe, modelPath y dos variants")
    parser.add_argument("--probe", type=Path, default=_default_probe(), help="qa_kv_cache compilado")
    parser.add_argument("--out", type=Path, default=Path("kv-cache-ab.json"))
    parser.add_argument("--log-dir", type=Path, default=None)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--contexts", default="8192,32768,131072")
    parser.add_argument("--depths", default="0.05,0.15,0.25,0.50,0.75,0.90,0.95")
    parser.add_argument("--users", type=int, default=1)
    # The deterministic passkey plus Qwen's short instruction preamble can
    # exceed 16 tokens. Keep enough headroom for exact-output scoring.
    parser.add_argument("--n-predict", type=int, default=32)
    parser.add_argument("--timeout-ms", type=int, default=120000)
    parser.add_argument("--startup-timeout", type=float, default=180.0)
    parser.add_argument("--passes", type=int, default=1)
    parser.add_argument("--include-aa", action="store_true",
                        help="repite baseline como control A/A y marca inestable si diverge")
    parser.add_argument("--no-metrics", action="store_true",
                        help="no agrega --metrics; sólo usar si el runtime no lo acepta")
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    try:
        config_path = Path(args.config).resolve()
        config = validate_config(json.loads(config_path.read_text(encoding="utf-8")))
        launcher = config["launcher"]
        if launcher["kind"] == "wsl":
            server_exe = config["serverExe"]
            model_path = config["modelPath"]
            _wsl_test_file(launcher["distro"], server_exe)
            _wsl_test_file(launcher["distro"], model_path)
            for variant in config["variants"]:
                _wsl_test_file(launcher["distro"], variant["serverExe"])
        else:
            server_exe = Path(config["serverExe"]).expanduser().resolve()
            model_path = Path(config["modelPath"]).expanduser().resolve()
            if not server_exe.is_file():
                raise ValueError(f"serverExe no existe: {server_exe}")
            if not model_path.is_file():
                raise ValueError(f"modelPath no existe: {model_path}")
            for variant in config["variants"]:
                variant_server = Path(variant["serverExe"]).expanduser().resolve()
                if not variant_server.is_file():
                    raise ValueError(
                        f"variants[{variant['id']}].serverExe no existe: {variant_server}")
        probe = args.probe.expanduser().resolve()
        if not probe.is_file():
            raise ValueError(f"qa_kv_cache no existe: {probe}")
        if not (1 <= args.passes <= 50):
            raise ValueError("passes debe estar entre 1 y 50")
        if not (1 <= args.users <= 256):
            raise ValueError("users debe estar entre 1 y 256")
        if not (1 <= args.n_predict <= 4096):
            raise ValueError("n-predict debe estar entre 1 y 4096")
        if not (1000 <= args.timeout_ms <= 3600000):
            raise ValueError("timeout-ms fuera de rango")
        if not (1024 <= args.port <= 65500 and args.port + 2 <= 65535):
            raise ValueError("port debe dejar dos puertos libres para las variantes")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    out_path = args.out.expanduser().resolve()
    log_dir = (args.log_dir.expanduser().resolve() if args.log_dir
               else out_path.parent / (out_path.stem + "-logs"))
    log_dir.mkdir(parents=True, exist_ok=True)
    variants = config["variants"]
    baseline, candidate = variants
    baseline_runs: List[Dict[str, Any]] = []
    candidate_runs: List[Dict[str, Any]] = []
    aa_runs: List[Dict[str, Any]] = []
    process_timeout = max(60.0, args.timeout_ms / 1000.0 *
                          max(1, len(args.contexts.split(","))) *
                          max(1, len(args.depths.split(","))) + 60.0)

    def run_variant(variant: Mapping[str, Any], port: int, pass_number: int,
                    label: str) -> Dict[str, Any]:
        variant_server: Any = variant["serverExe"]
        if launcher["kind"] == "native":
            variant_server = Path(variant_server).expanduser().resolve()
        variant_args = normalize_server_args(
            config["commonArgs"] + variant["args"], str(model_path), args.host, port,
            add_metrics=not args.no_metrics)
        base_url = _join_url(args.host, port)
        log_path = log_dir / f"pass-{pass_number:03d}-{label}.log"
        record: Dict[str, Any] = {
            "pass": pass_number, "variant": label, "id": variant["id"],
            "serverExe": str(variant_server),
            "port": port, "baseUrl": base_url,
            "command": [str(variant_server), *variant_args],
            "envKeys": sorted(variant["env"].keys()), "logPath": str(log_path),
            "launcher": launcher,
        }
        server: Optional[RunningServer] = None
        try:
            server = start_server(variant_server, variant_args, variant["env"], log_path,
                                  launcher)
            wait_for_server(base_url, args.startup_timeout, server.process)
            receipt, probe_exit, probe_stderr = run_probe(
                probe, base_url, args.contexts, args.depths, args.users,
                args.n_predict, args.timeout_ms, process_timeout)
            record.update({"probeExitCode": probe_exit, "probeStderr": probe_stderr,
                           "receipt": receipt, "passed": probe_exit == 0 and
                           bool(receipt.get("summary", {}).get("allPassed"))})
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            detail = str(error)
            tail = _log_tail(log_path)
            if tail:
                detail += f"\nlog tail:\n{tail}"
            record.update({"passed": False, "error": detail})
        finally:
            if server is not None:
                record["serverExitCode"] = server.stop()
        return record

    started_at = time.time()
    for pass_number in range(1, args.passes + 1):
        print(f"[kv-cache-ab] pasada {pass_number}/{args.passes}: baseline", file=sys.stderr)
        baseline_record = run_variant(baseline, args.port, pass_number, "baseline")
        baseline_runs.append(baseline_record)
        if args.include_aa:
            print(f"[kv-cache-ab] pasada {pass_number}/{args.passes}: control A/A", file=sys.stderr)
            aa_runs.append(run_variant(baseline, args.port + 2, pass_number, "aa-control"))
        print(f"[kv-cache-ab] pasada {pass_number}/{args.passes}: candidate", file=sys.stderr)
        candidate_runs.append(run_variant(candidate, args.port + 1, pass_number, "candidate"))

    comparison = compare_run_records(baseline_runs, candidate_runs)
    aa_comparison = None
    if args.include_aa:
        aa_comparison = compare_run_records(baseline_runs, aa_runs)

    baseline_ok = any("receipt" in item for item in baseline_runs) \
        and all(item.get("passed") for item in baseline_runs)
    candidate_ok = any("receipt" in item for item in candidate_runs) \
        and all(item.get("passed") for item in candidate_runs)
    aa_ok = True
    if args.include_aa:
        aa_ok = bool(aa_comparison and aa_comparison["pairCount"] >= 2 and
                     not aa_comparison["genTps"]["significant"])
    report = {
        "schema": SCHEMA,
        "generatedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "elapsedSec": time.time() - started_at,
        "configPath": str(config_path),
        "probe": str(probe),
        "serverExe": str(server_exe),
        "modelPath": str(model_path),
        "launcher": launcher,
        "host": args.host, "basePort": args.port,
        "contexts": args.contexts, "depths": args.depths, "users": args.users,
        "nPredict": args.n_predict, "timeoutMs": args.timeout_ms,
        "passes": args.passes, "includeAa": args.include_aa,
        "variants": [
            {"id": baseline["id"], "serverExe": baseline["serverExe"],
             "args": baseline["args"], "envKeys": sorted(baseline["env"].keys())},
            {"id": candidate["id"], "serverExe": candidate["serverExe"],
             "args": candidate["args"], "envKeys": sorted(candidate["env"].keys())},
        ],
        "baselineRuns": baseline_runs, "candidateRuns": candidate_runs,
        "comparison": comparison, "aaComparison": aa_comparison,
        "summary": {
            "baselineAllPassed": baseline_ok,
            "candidateAllPassed": candidate_ok,
            "aaPassed": aa_ok,
            "allPassed": baseline_ok and candidate_ok and aa_ok and
                         not comparison["unmatchedBaseline"] and
                         not comparison["unmatchedCandidate"],
            "validForPromotion": baseline_ok and candidate_ok and aa_ok and
                                 comparison["pairCount"] >= 2,
        },
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(report["summary"], indent=2, ensure_ascii=False))
    print(f"Informe: {out_path}", file=sys.stderr)
    return 0 if report["summary"]["allPassed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
