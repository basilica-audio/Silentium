#!/usr/bin/env bash
#
# Assert that a pluginval run genuinely validated a plugin and genuinely passed.
#
# Usage: assert-pluginval-passed.sh <log-file> <pluginval-exit-code> <label>
#
# Why this exists
# ---------------
# pluginval documents "0 if all tests complete successfully, 1 if there are any
# errors" (Tracktion/pluginval v1.0.4, Source/CommandLine.cpp, getHelpMessage()).
# Trusting that exit code alone is not sufficient, because the exit code can be
# lost on the way back to the runner. On Windows pluginval is built with
# juce_add_gui_app, i.e. a GUI-subsystem binary, and PowerShell's call operator
# does not wait for those and leaves $LASTEXITCODE unset. A run that printed
# "Num plugins found: 0", "FAILURE" and "*** FAILED" therefore still reported a
# green job for the entire life of the workflow -- see basilica-audio/Nave#36.
#
# So the gate is asserted from several independent signals. Any one of them
# failing fails the job. In particular the plugin-count check means a validator
# that scanned the wrong path and found nothing can never read as success again,
# whatever the exit code says.
#
# The relevant markers are emitted unconditionally by pluginval:
#   - "Num plugins found: N"  -- Source/PluginTests.cpp, PluginTests::runTest()
#   - "SUCCESS" or "FAILURE"  -- Source/Validator.cpp, runTests()
set -euo pipefail

LOG=${1:?usage: assert-pluginval-passed.sh <log-file> <pluginval-exit-code> <label>}
EXIT_CODE=${2:?usage: assert-pluginval-passed.sh <log-file> <pluginval-exit-code> <label>}
LABEL=${3:-pluginval}

fail() {
  echo "::error::pluginval [${LABEL}] ${1}"
  exit 1
}

# --- 0. The run must have produced output at all. -----------------------------
# An empty log means either that pluginval never started or that its output was
# not captured, and in both cases the checks below would vacuously pass.
if [ ! -s "$LOG" ]; then
  fail "produced no output: '${LOG}' is missing or empty, so the validation either did not run or its output was not captured."
fi

# Normalise CRLF so the anchored patterns below behave identically on both
# runners (the Windows log is written by a native Windows process).
NORMALISED=$(mktemp)
trap 'rm -f "$NORMALISED"' EXIT
tr -d '\r' < "$LOG" > "$NORMALISED"

# --- 1. A plugin must actually have been found. -------------------------------
# This is the guard that prevents the Nave#36 class of silent regression coming
# back under a different path typo: a validator that finds nothing is a failure,
# never a pass.
COUNT_LINE=$(grep -E 'Num plugins found:[[:space:]]*[0-9]+' "$NORMALISED" | tail -n 1 || true)
if [ -z "$COUNT_LINE" ]; then
  fail "output contains no 'Num plugins found:' line, so it never scanned the target."
fi

PLUGIN_COUNT=$(printf '%s' "$COUNT_LINE" | sed -E 's/.*Num plugins found:[[:space:]]*([0-9]+).*/\1/')
if ! [ "$PLUGIN_COUNT" -ge 1 ] 2>/dev/null; then
  fail "found ${PLUGIN_COUNT} plugin(s) at the scanned path. The artefact path is wrong, or the binary is missing, damaged or unloadable."
fi

# --- 2. No failure markers, independent of the exit code. ---------------------
MARKERS=$(grep -nE '^\*\*\* FAILED|^FAILURE$|^FAILED!!|^!!! Test .+ failed' "$NORMALISED" || true)
if [ -n "$MARKERS" ]; then
  echo "$MARKERS"
  fail "reported test failures (see the marked lines above)."
fi

# --- 3. The positive marker must be present. ----------------------------------
# pluginval logs exactly one of SUCCESS / FAILURE per run, so requiring SUCCESS
# also catches a run that was killed part-way through and produced neither.
if ! grep -qE '^SUCCESS$' "$NORMALISED"; then
  fail "did not report SUCCESS, so the run did not complete cleanly."
fi

# --- 4. And finally the process exit code itself. -----------------------------
if [ "$EXIT_CODE" != "0" ]; then
  fail "exited with code ${EXIT_CODE}."
fi

echo "pluginval [${LABEL}] OK: ${PLUGIN_COUNT} plugin(s) found, SUCCESS reported, exit code 0."
