from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from llamacode_harness import WorkerRuntime  # noqa: E402


def echo(payload, _context):
    return {"value": payload.get("value"), "lane": "python"}


try:
    WorkerRuntime({"echo": echo}).run()
except Exception as exc:
    print(exc, file=sys.stderr)
    raise
