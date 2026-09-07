---
description: Close the test-coverage gap — find features/changes shipped without a test, write the missing tests, and loop the gate until green.
---

# Test-coverage loop

Enforce the "sin test = incompleto" policy mechanically. Find changes that lack
a test, add the test, run the gate, repeat until coverage is complete and green.

Scope (optional argument): $ARGUMENTS
- no arg → diff of the current branch vs `main`
- `today` → commits since midnight
- a path/module name → focus there

## Loop

1. **Find candidates.** `git diff --stat <scope>` (default: `main...HEAD`).
   List touched subsystems. For each, check whether a behavior changed (new
   class/method, new ControlApi verb, new tool, new backend path).

2. **Cross-check coverage.** Map each touched subsystem to its test using the
   module→test table in CLAUDE.md and the `add_lc_test(...)` lines in
   `CMakeLists.txt`. A subsystem with a behavior change and no corresponding
   test edit = a coverage gap.

3. **Write the missing test.** One file `tests/test_<area>.cpp`, one
   `QTEST_MAIN`. Honor conventions (test mode, `LLAMACODE_PROFILES_DIR` before
   first `ProfileManager`, clean catalog DB if needed, pump event loop for net
   backends, `QSignalSpy` for `toolExecuted`). Register with
   `add_lc_test(test_<area> tests/test_<area>.cpp)` in the `if (BUILD_TESTS)`
   section.

4. **Run the gate.** Invoke `/gate`. Red → fix and stay on this candidate.
   Green → next candidate.

5. **Repeat** until no gaps remain and the gate is green.

## Output

End with: gaps found, tests added (file + `add_lc_test` line), final gate
verdict. If no gaps: `COVERAGE OK — no untested changes in <scope>`.

## Notes

- Known uncovered path (CLAUDE.md "Pendiente de cobertura"): real SSE stream for
  network backends needs an HTTP stub for `/v1/chat/completions` and
  `/v1/embeddings`. If a touched path needs it, build the stub as part of the loop.
- Do not commit from this loop unless the user asks; report and let them decide.
