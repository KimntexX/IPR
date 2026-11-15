<!-- translation-source: docs/validation/tools/surveillance-tests.md; sha256: 3c42fa77535bf9138d287de3911b791c5f3b95ea713cf3683fe106139bfd0790 -->
*[Français](surveillance-tests.md) | [English](surveillance-tests_en.md)*

# Test completion and automatic analysis

Status on 1 November 2026: runner prepared on `patch_raw_bench`, file
`scripts/run_test_then_analyse.py`. Python standard library only. No GUI or
photographic engine integration and no installed scheduled task.

## Behaviour

The runner executes a foreground test command, waits for its process through the
operating system, then records its status and logs. No model is queried while
waiting. The test must wait for its own children: do not supply a launcher that
detaches the actual computation and exits immediately.

The explicit `--analyse` option then allows **one** `codex exec` invocation,
with a `read-only` sandbox, no approval request and no conversation resumption.
Its answer is stored in `analysis.md`, not injected into the existing chat.
Without this option, no model call is made.

The CLI must be installed and authenticated. `--codex` may specify its absolute
path if PATH discovery fails. The runner installs nothing and collects no API
key. It ignores the Codex user configuration but uses its authentication
mechanism; no particular model is imposed. Local version capabilities and
policies still apply.

## Usage

From the worktree containing the script, run its targeted tests without AI:

```powershell
python scripts/run_test_then_analyse.py --output-dir build-patch_raw_bench/jobs/check-01 -- python -m unittest discover -s tests -p test_run_test_then_analyse.py
```

To authorize a real analysis after the same kind of test:

```powershell
python scripts/run_test_then_analyse.py --analyse --output-dir build-patch_raw_bench/jobs/check-02 -- python -m unittest discover -s tests -p test_run_test_then_analyse.py
```

Replace the final program with the exact next campaign command. Add
`--result path/to/status-or-result.json` **before** `--` to identify the JSON it
will produce. Relative paths resolve within `--workspace` (default: the script's
worktree). Both the expected result and output directory must stay inside that
workspace. Adjust the Python interpreter to the local installation.

The output directory must be **new** for every run. Reuse is refused, including
after an interruption, to avoid accidental duplicate calls. Network errors,
quota failures and timeouts never trigger automatic retries. Do not put secrets
in test arguments or logs.

## Files and safeguards

- `completion.json`: current phase, exit code, timeout and duration.
- `test.stdout.log`, `test.stderr.log`: test program output.
- `analysis-prompt.txt`: instructions and paths given to the model.
- With AI: `analysis.events.jsonl`, `analysis.stderr.log`, and `analysis.md`
  if the final answer was actually produced.

Controller success is not photographic validation. The model may explain a
failed test without hiding its failure code. Runner exit codes: 0 = test passed
and optional analysis finalized; 1 = test failed; 2 = test timeout or usage
error; 3 = analysis not finalized. Unexpected errors are recorded and may
produce another nonzero exit code.

Default timeouts: 24 hours for the test, 15 minutes for analysis; options
`--test-timeout` and `--analysis-timeout` use seconds. On interruption or timeout,
the runner stops its active child and attempts descendant cleanup (`/T` on
Windows, process group on POSIX), without searching for other agents by name.
Commands detaching children are unsupported. Cleanup failures surface without
an implicit unbounded wait.

Analysis instructions restrict work to the specified logs and JSON evidence,
without images, network, new campaigns, edits, Git, external messages or sleep.
The script attaches no photographs. With `--analyse`, context and material read
by Codex are processed through its service, not local model inference. The
sandbox protects model commands; instructions do not qualify the security of
all integrations in an environment.

**This runner does not put the computer to sleep.** If paired with a sleep
controller, wait for the entire runner, including analysis, not just the test.
The old watcher pinned to PID 4008 cannot be reused for a new campaign.

## Qualification

Twelve targeted tests pass: success, failure, test/analysis timeouts, exclusive
output, invalid arguments, exactly one fake CLI invocation, startup failure and
fallback after Windows cleanup timeout. A non-AI runner CLI smoke test passes.
No real model call, quota or account access was tested; the first live use
still needs qualification. Windows tested, POSIX unqualified.

References: [Codex non-interactive mode](https://learn.chatgpt.com/docs/non-interactive-mode)
and [CLI options](https://learn.chatgpt.com/docs/cli/reference).
