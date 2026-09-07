---
name: gate
description: Quality gate for LlamaCode — runs the full test suite (Release) the canonical way and reports pass/fail. Use before any commit, or when the user says "gate", "run the gate", "quality gate", "check tests before commit", or invokes /gate. Enforces the "sin test = incompleto" policy.
---

# LlamaCode quality gate

Single invocable that runs the test suite exactly the way the project requires,
so conventions never get re-derived. This is the pre-commit gate: **build + all
tests green = OK to commit. Red = stop, do not commit.**

## What this gate guarantees

The policy (CLAUDE.md): every new feature or behavior change ships with its test
(unit + integration when applicable). No test = incomplete. The gate is how that
policy is enforced mechanically.

## Steps

1. **Run the suite (Release-only policy):**
   ```
   tests.bat Release
   ```
   - `tests.bat` configures `BUILD_TESTS=ON`, builds in `build_tests/`, runs
     `ctest`. It has no `pause` → safe to run from a tool. Invoke from the repo
     root via PowerShell: `.\tests.bat Release` (heavy: full Release build +
     ctest, run in background and wait for the notification).
   - Do **not** build Debug (Release-only policy since 2026-06-18).

2. **Read the result. Three outcomes, not two:**
   - Configure/Build failure → report the failing target and stop. This is a red
     gate (do not commit). **Before blaming your own change:** several sessions
     share this working tree, so the broken file may be someone else's work in
     progress. Check `git status` — if the failing file isn't yours, say so
     instead of "fixing" it.
   - `ctest` runs with `--output-on-failure`. Any failing test → red gate.
     Report which test(s) failed, quote the assertion/output exactly.
   - **`=== All tests passed ===` but the log ends with `[WARN] ... DIRTY`** →
     **NOT green. Inconclusive.** `build_coord.ps1` re-fingerprints the source on
     release; DIRTY means another session edited `src/`/`qml/`/`tests/` while the
     suite was running, so those tests did not run on the source you are about to
     commit. `tests.bat` still exits 0 (its contract is "the tests I ran passed"),
     so **the exit code cannot be trusted here — you must read the log tail.**
   - All green **and** no DIRTY warning → green gate.

3. **Report** a one-line verdict first, then details only if not green:
   - `GATE GREEN — N/N tests passed`
   - `GATE RED — <test> failed`
   - `GATE DIRTY — N/N passed but the source moved mid-run; result does not count`

   Never report a DIRTY run as green, and never soften it to "green with a
   caveat" — the whole point is that the number is meaningless. Reporting a
   bogus green is the exact failure this project keeps hitting.

4. **When DIRTY:** do not loop blindly — with several sessions active, most runs
   on the shared tree will come back DIRTY. Say it plainly and offer the two real
   options: re-run and hope the tree is quiet, or isolate first with
   `powershell -File worktree.ps1 -Action new -Name <tarea>` and run the gate
   there (a worktree is the only way to get a gate that actually means something).
   If your change touches no C++/CMake at all, say that too: ctest is orthogonal
   to it, and that is a more useful statement than a number.

## Conventions the gate assumes (do not re-explain, just honor)

- **Disk isolation:** tests use `QStandardPaths::setTestModeEnabled(true)` to
  redirect AppData/AppLocalData. Profiles use env var `LLAMACODE_PROFILES_DIR`,
  set in `initTestCase` **before** the first `ProfileManager` (root cached in a
  `static`).
- **Clean state:** the catalog DB persists between runs. If a test needs clean
  state it must delete the DB in `initTestCase`. If you see flaky catalog tests,
  that's the cause — fix it in the test, not in the gate.
- **Network backends:** server + client run on the same thread → never
  `waitForReadyRead`; pump the event loop (`QCoreApplication::processEvents`).
- **AgentToolRunner:** `executeTool` emits `toolExecuted`; capture with
  `QSignalSpy`. `run_shell` is async (wait on the spy).
- **One QtTest exe per subsystem** (one `QTEST_MAIN` per binary). New test =
  one file in `tests/` + one `add_lc_test(test_<area> tests/test_<area>.cpp)`
  line in `CMakeLists.txt` (section `if (BUILD_TESTS)`).

## When the gate is red

Do not commit. Either fix the failing test/code or, if a feature is missing its
test, write it (see the module→test map in CLAUDE.md), register it, and re-run
the gate. Loop until green.

## Why DIRTY exists (do not "simplify" it away)

The lock in `build_coord.ps1` serializes *who compiles*, not *what source is on
disk*: other sessions edit the tree outside the lock, so a run can compile a
source that no longer exists. A green that did not run on your source is worse
than a red — it is a number that reads as evidence and is not. `tests.bat` keeps
exit 0 on DIRTY on purpose: making it non-zero would paint the gate red almost
permanently (with several sessions, any multi-minute run gets touched), and a red
nobody can act on trains everyone to ignore red — including the real ones. So the
honesty lives here, in the reporting, not in the exit code.
