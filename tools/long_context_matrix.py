"""Run the long-context probe once per server context size.

Unlike ``qa_kv_cache`` alone, this runner restarts llama-server for every
context.  That matters for experiments such as 131K/256K/512K/1M: a probe
request cannot increase the KV allocation of an already-running server.
"""

from __future__ import annotations

import argparse
import json
import math
import time
from pathlib import Path, PurePosixPath
import sys
from typing import Any, Dict, List, Mapping, Optional, Sequence

# When invoked as ``python tools/long_context_matrix.py``, Python puts the
# tools directory (not the repository root) first on sys.path.
_ROOT = Path(__file__).resolve().parents[1]
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from tools.kv_cache_ab import (
    RunningServer,
    _join_url,
    _wsl_test_file,
    normalize_server_args,
    run_probe,
    start_server,
    wait_for_server,
)


SCHEMA = "llamacode-long-context-matrix-v1"
CONTEXT_FLAGS = {"--ctx-size", "--n-ctx", "-c", "--fit-ctx"}
MAX_CONTEXT = 1_048_576


def parse_contexts(raw: str) -> List[int]:
    """Parse a comma-separated context list without silently deduplicating it."""
    values: List[int] = []
    for part in raw.split(","):
        text = part.strip()
        if not text:
            continue
        try:
            value = int(text)
        except ValueError as error:
            raise ValueError(f"contexto invalido: {text}") from error
        if value <= 0 or value > MAX_CONTEXT:
            raise ValueError(f"contexto fuera de rango (1..{MAX_CONTEXT}): {value}")
        if value in values:
            raise ValueError(f"contexto duplicado: {value}")
        values.append(value)
    if not values:
        raise ValueError("contexts debe contener al menos un contexto")
    return values


def parse_depths(raw: str) -> List[float]:
    values: List[float] = []
    for part in raw.split(","):
        text = part.strip()
        if not text:
            continue
        try:
            value = float(text)
        except ValueError as error:
            raise ValueError(f"profundidad invalida: {text}") from error
        if not math.isfinite(value) or value < 0.0 or value > 1.0:
            raise ValueError(f"profundidad fuera de rango [0..1]: {text}")
        values.append(value)
    if not values:
        raise ValueError("depths debe contener al menos una profundidad")
    return values


def _replace_value_flag(args: Sequence[str], flag: str, value: str) -> tuple[List[str], int]:
    out: List[str] = []
    found = 0
    index = 0
    while index < len(args):
        token = str(args[index])
        if token == flag:
            if index + 1 >= len(args):
                raise ValueError(f"{flag} no tiene valor")
            out.extend([flag, value])
            found += 1
            index += 2
            continue
        if token.startswith(flag + "="):
            out.append(flag + "=" + value)
            found += 1
        else:
            out.append(token)
        index += 1
    return out, found


def render_context_args(args: Sequence[str], context: int) -> List[str]:
    """Render one server command, replacing all allocation context flags.

    ``{context}`` is supported for configs that do not use a conventional
    flag.  Conventional flags are still rewritten so a copied LID profile
    with ``--fit-ctx 131072`` cannot accidentally under-test a larger preset.
    """
    if context <= 0 or context > MAX_CONTEXT:
        raise ValueError(f"contexto fuera de rango: {context}")
    rendered = [str(arg).replace("{context}", str(context)) for arg in args]
    for flag in CONTEXT_FLAGS:
        # Multiple flags are allowed when they are the same allocation value
        # (for example --ctx-size plus --fit-ctx in the LID recipe).
        rendered, _ = _replace_value_flag(rendered, flag, str(context))
    if not any(token == "--ctx-size" or token.startswith("--ctx-size=")
               for token in rendered):
        rendered.extend(["--ctx-size", str(context)])
    return rendered


def _as_string_list(value: Any, field: str) -> List[str]:
    if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
        raise ValueError(f"{field} debe ser una lista de strings")
    return list(value)


def validate_config(config: Mapping[str, Any]) -> Dict[str, Any]:
    server = str(config.get("serverExe", "")).strip()
    model = str(config.get("modelPath", "")).strip()
    if not server:
        raise ValueError("Falta serverExe en la configuración")
    if not model:
        raise ValueError("Falta modelPath en la configuración")
    launcher_value = config.get("launcher", {}) or {}
    if not isinstance(launcher_value, dict):
        raise ValueError("launcher debe ser un objeto")
    kind = str(launcher_value.get("kind", "native")).strip().lower()
    if kind not in {"native", "wsl"}:
        raise ValueError("launcher.kind debe ser native o wsl")
    launcher: Dict[str, str] = {"kind": kind}
    if kind == "wsl":
        distro = str(launcher_value.get("distro", "")).strip()
        if not distro:
            raise ValueError("launcher.distro es obligatorio para WSL")
        if not PurePosixPath(server).is_absolute() or not PurePosixPath(model).is_absolute():
            raise ValueError("serverExe y modelPath deben ser rutas absolutas Linux para WSL")
        launcher["distro"] = distro
        cwd = str(launcher_value.get("cwd", "")).strip()
        if cwd:
            if not PurePosixPath(cwd).is_absolute():
                raise ValueError("launcher.cwd debe ser una ruta absoluta Linux")
            launcher["cwd"] = cwd
    variant = config.get("variant")
    if not isinstance(variant, dict):
        raise ValueError("variant debe ser un objeto")
    variant_id = str(variant.get("id", "")).strip()
    if not variant_id:
        raise ValueError("variant.id es obligatorio")
    env = variant.get("env", {})
    if not isinstance(env, dict) or any(
        not isinstance(key, str) or not isinstance(value, str) for key, value in env.items()
    ):
        raise ValueError("variant.env debe ser un objeto string -> string")
    contexts = config.get("contexts")
    if not isinstance(contexts, list) or not contexts:
        raise ValueError("contexts debe ser una lista no vacia")
    parsed = parse_contexts(",".join(str(item) for item in contexts))
    return {
        "schema": str(config.get("schema", SCHEMA)),
        "serverExe": server,
        "modelPath": model,
        "commonArgs": _as_string_list(config.get("commonArgs", []), "commonArgs"),
        "launcher": launcher,
        "variant": {
            "id": variant_id,
            "args": _as_string_list(variant.get("args", []), "variant.args"),
            "env": dict(env),
        },
        "contexts": parsed,
    }


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--probe", type=Path, default=None)
    parser.add_argument("--contexts", default=None,
                        help="override comma-separated contexts from config")
    parser.add_argument("--depths", default="0.05,0.15,0.25,0.50,0.75,0.90,0.95")
    parser.add_argument("--users", type=int, default=1)
    parser.add_argument("--n-predict", type=int, default=32)
    parser.add_argument("--timeout-ms", type=int, default=120000)
    parser.add_argument("--startup-timeout", type=float, default=180.0)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--out", type=Path, default=Path("long-context-matrix.json"))
    parser.add_argument("--log-dir", type=Path, default=None)
    parser.add_argument("--no-metrics", action="store_true")
    parser.add_argument("--startup-only", action="store_true",
                        help="verifica el arranque saludable de cada contexto sin ejecutar qa_kv_cache")
    parser.add_argument("--dry-run", action="store_true",
                        help="validar y mostrar los comandos sin iniciar servidores")
    return parser.parse_args()


def _default_probe() -> Path:
    root = Path(__file__).resolve().parents[1]
    return root / "build" / "Debug" / "qa_kv_cache.exe"


def main() -> int:
    args = _parse_args()
    try:
        config_path = args.config.expanduser().resolve()
        config = validate_config(json.loads(config_path.read_text(encoding="utf-8")))
        contexts = parse_contexts(args.contexts) if args.contexts else config["contexts"]
        parse_depths(args.depths)
        if not 1 <= args.users <= 256 or not 1 <= args.n_predict <= 4096:
            raise ValueError("users/n-predict fuera de rango")
        if not 1000 <= args.timeout_ms <= 3600000:
            raise ValueError("timeout-ms fuera de rango")
        if not 1024 <= args.port <= 65533:
            raise ValueError("port fuera de rango")
        probe = (args.probe or _default_probe()).expanduser().resolve()
        if not args.dry_run and not args.startup_only and not probe.is_file():
            raise ValueError(f"qa_kv_cache no existe: {probe}")
        launcher = config["launcher"]
        if launcher["kind"] == "wsl" and not args.dry_run:
            _wsl_test_file(launcher["distro"], config["serverExe"])
            _wsl_test_file(launcher["distro"], config["modelPath"])
            server_exe: Any = config["serverExe"]
            model_path = config["modelPath"]
        else:
            server_exe = Path(config["serverExe"]).expanduser().resolve()
            model_path = Path(config["modelPath"]).expanduser().resolve()
            if not args.dry_run:
                if not server_exe.is_file():
                    raise ValueError(f"serverExe no existe: {server_exe}")
                if not model_path.is_file():
                    raise ValueError(f"modelPath no existe: {model_path}")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    base_args = config["commonArgs"] + config["variant"]["args"]
    commands: List[Dict[str, Any]] = []
    for context in contexts:
        rendered = normalize_server_args(
            render_context_args(base_args, context), str(model_path), args.host, args.port,
            add_metrics=not args.no_metrics)
        commands.append({"contextTokens": context, "command": [str(server_exe), *rendered]})

    if args.dry_run:
        print(json.dumps({"schema": SCHEMA, "contexts": contexts, "commands": commands},
                         indent=2, ensure_ascii=False))
        return 0

    out_path = args.out.expanduser().resolve()
    log_dir = (args.log_dir.expanduser().resolve() if args.log_dir
               else out_path.parent / (out_path.stem + "-logs"))
    log_dir.mkdir(parents=True, exist_ok=True)
    rows: List[Dict[str, Any]] = []
    started_at = time.time()
    for item in commands:
        context = item["contextTokens"]
        log_path = log_dir / f"context-{context}.log"
        record: Dict[str, Any] = {
            "contextTokens": context,
            "variant": config["variant"]["id"],
            "command": item["command"],
            "envKeys": sorted(config["variant"]["env"].keys()),
            "logPath": str(log_path),
            "passed": False,
            "startupOnly": args.startup_only,
        }
        server: Optional[RunningServer] = None
        try:
            server = start_server(server_exe, item["command"][1:],
                                  config["variant"]["env"], log_path, launcher)
            base_url = _join_url(args.host, args.port)
            wait_for_server(base_url, args.startup_timeout, server.process)
            if args.startup_only:
                record["passed"] = True
            else:
                receipt, probe_exit, probe_stderr = run_probe(
                    probe, base_url, str(context), args.depths, args.users,
                    args.n_predict, args.timeout_ms,
                    max(60.0, args.timeout_ms / 1000.0 * max(1, len(args.depths.split(","))) + 60.0),
                )
                passed = probe_exit == 0 and bool(receipt.get("summary", {}).get("allPassed"))
                record.update({"probeExitCode": probe_exit, "probeStderr": probe_stderr,
                               "receipt": receipt, "passed": passed})
                if not passed:
                    record["error"] = "el probe no pasó la recuperación exacta"
        except (OSError, RuntimeError, ValueError) as error:
            record["error"] = str(error)
        finally:
            if server is not None:
                record["serverExitCode"] = server.stop()
        rows.append(record)
        print(f"[long-context-matrix] {context}: "
              f"{'PASS' if record['passed'] else 'FAIL'}", file=sys.stderr)

    passed_contexts = [row["contextTokens"] for row in rows if row["passed"]]
    report = {
        "schema": SCHEMA,
        "generatedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "elapsedSec": time.time() - started_at,
        "configPath": str(config_path),
        "probe": str(probe),
        "launcher": launcher,
        "variant": config["variant"]["id"],
        "serverExe": config["serverExe"],
        "modelPath": config["modelPath"],
        "host": args.host,
        "port": args.port,
        "contexts": contexts,
        "depths": args.depths,
        "users": args.users,
        "nPredict": args.n_predict,
        "timeoutMs": args.timeout_ms,
        "startupOnly": args.startup_only,
        "startupTimeoutSec": args.startup_timeout,
        "rows": rows,
        "summary": {
            "allPassed": len(rows) == len(contexts) and bool(rows) and len(passed_contexts) == len(contexts),
            "verifiedContexts": passed_contexts,
            "unverifiedContexts": [context for context in contexts if context not in passed_contexts],
        },
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(report["summary"], indent=2, ensure_ascii=False))
    print(f"Informe: {out_path}", file=sys.stderr)
    return 0 if report["summary"]["allPassed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
