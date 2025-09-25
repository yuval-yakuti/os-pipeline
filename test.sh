#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

pass() { :; }
fail() { echo "FAIL: $*" >&2; exit 1; }

# Build (quiet)
./build.sh >/dev/null 2>&1

# Speed up typewriter for tests
export TYPEWRITER_DELAY_US=1000

# 1) logger
rm -f output/pipeline.log
echo "hello world" | ./build/pipeline logger >/dev/null 2>&1
# Wait briefly for file to be written to avoid flakiness on some systems
for _ in 1 2 3 4 5; do
  last_line="$(tail -n 1 output/pipeline.log 2>/dev/null || true)"
  [[ -n "$last_line" ]] && break
  sleep 0.05
done
if [[ "${last_line}" != "hello world" ]]; then
  fail "logger: expected last log line 'hello world', got '${last_line}'"
fi
pass "logger"

# 2) uppercaser (use sink_stdout for explicit stdout capture)
out="$(echo "Hello World" | ./build/pipeline uppercaser,logger >/dev/null 2>&1; tail -n 1 output/pipeline.log)"
if [[ "${out}" != "HELLO WORLD" ]]; then
  fail "uppercaser: expected 'HELLO WORLD', got '${out}'"
fi
pass "uppercaser"

# 3) rotator with ROTATE_N=5
out="$(ROTATE_N=5 ./build/pipeline rotator,logger <<< "Abc XyZ" >/dev/null 2>&1; tail -n 1 output/pipeline.log)"
if [[ "${out}" != "Fgh CdE" ]]; then
  fail "rotator: expected 'Fgh CdE', got '${out}'"
fi
pass "rotator"

# 4) expander→uppercaser→flipper
out="$(printf "  a\t  b   c  \n" | ./build/pipeline expander,uppercaser,flipper,logger >/dev/null 2>&1; tail -n 1 output/pipeline.log)"
if [[ "${out}" != "C B A" ]]; then
  fail "expander,uppercaser,flipper: expected 'C B A', got '${out}'"
fi
pass "expander→uppercaser→flipper"

# 5) missing plugin must fail
if ./build/pipeline not_a_plugin >/dev/null 2>&1; then
  fail "missing plugin: pipeline should fail when plugin is not found"
fi
pass "missing plugin"

# 6) sink_stdout prints line and respects <END>
out="$(printf "ab cd\n<END>\n" | ./build/pipeline sink_stdout)"
if [[ "${out}" != "ab cd"$'' ]]; then
  fail "sink_stdout: expected 'ab cd', got '${out}'"
fi
pass "sink_stdout basic"

# 7) sink_stdout EOF-only: only <END> produces no output
out="$(printf "<END>\n" | ./build/pipeline sink_stdout)"
if [[ -n "${out}" ]]; then
  fail "sink_stdout EOF-only: expected empty output, got '${out}'"
fi
pass "sink_stdout EOF-only"

# 8) missing plugin emits clear error
set +e
err_out=$(./build/pipeline this_plugin_should_not_exist 2>&1 >/dev/null)
rc=$?
set -e
if [[ $rc -eq 0 ]]; then
  fail "missing plugin should fail"
fi
if ! echo "$err_out" | grep -E "(dlopen failed|missing required symbols)" >/dev/null; then
  fail "missing plugin error message not clear: $err_out"
fi
pass "missing plugin error message"

echo "All smoke tests passed."


