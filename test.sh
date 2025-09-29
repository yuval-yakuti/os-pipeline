#!/usr/bin/env bash
set -euo pipefail
# Disable job control notifications to avoid 'Terminated: 15' lines
set +m

cd "$(dirname "$0")"

pass() { :; }
fail() { echo "FAIL: $*" >&2; exit 1; }

# Timeout helper (argv-safe)
run_with_timeout() {
  ( "$@" ) & p=$!
  ( sleep 3; kill -0 "$p" 2>/dev/null && kill "$p" >/dev/null 2>&1 || true ) >/dev/null 2>&1 &
  w=$!
  disown "$w" 2>/dev/null || true
  # Preserve child exit status (do not mask with || true)
  set +e
  wait "$p"
  rc=$?
  set -e
  kill "$w" >/dev/null 2>&1 || true
  return $rc
}

# Timeout helper with custom seconds: run_with_timeout_n SECONDS cmd...
run_with_timeout_n() {
  local secs="$1"; shift
  ( "$@" ) & p=$!
  ( sleep "$secs"; kill -0 "$p" 2>/dev/null && kill "$p" >/dev/null 2>&1 || true ) >/dev/null 2>&1 &
  w=$!
  disown "$w" 2>/dev/null || true
  set +e
  wait "$p"; rc=$?
  set -e
  kill "$w" >/dev/null 2>&1 || true
  return $rc
}

# Poller for logger file to avoid stale tail
wait_for_log_line() {
  local tries=40
  while (( tries-- > 0 )); do
    local last="$(tail -n1 output/pipeline.log 2>/dev/null || true)"
    [[ -n "$last" ]] && { printf '%s\n' "$last"; return 0; }
    sleep 0.1
  done
  printf '\n'; return 1
}

# Build (quiet)
./build.sh >/dev/null 2>&1

# Speed up typewriter for tests
export TYPEWRITER_DELAY_US=1000

# 1) logger
: > output/pipeline.log
run_with_timeout sh -c 'printf "hello world\n<END>\n" | ./build/pipeline logger >/dev/null 2>&1'
last_line="$(wait_for_log_line || true)"
if [[ "${last_line}" != "hello world" ]]; then
  fail "logger: expected last log line 'hello world', got '${last_line}'"
fi
pass "logger"

# 2) uppercaser (logger -> file)
: > output/pipeline.log
run_with_timeout sh -c 'printf "Hello World\n<END>\n" | ./build/pipeline uppercaser,logger >/dev/null 2>&1'
out="$(wait_for_log_line || true)"
if [[ "${out}" != "HELLO WORLD" ]]; then
  fail "uppercaser: expected 'HELLO WORLD', got '${out}'"
fi
pass "uppercaser"

# 3) rotator rotate-right-by-one (assert via logger; robust under runner)
: > output/pipeline.log
run_with_timeout sh -c 'printf "Abc XyZ\n<END>\n" | ./build/pipeline rotator,logger >/dev/null 2>rot.err'
out="$(wait_for_log_line || true)"
if [[ "${out}" != "ZAbc Xy" ]]; then
  echo '--- rot.err ---'
  cat rot.err 2>/dev/null || true
  echo '--- log tail ---'
  tail -n 5 output/pipeline.log 2>/dev/null || true
  fail "rotator: expected 'ZAbc Xy', got '${out}'"
fi
pass "rotator"

# 4) expander→uppercaser→flipper
: > output/pipeline.log
run_with_timeout sh -c 'printf "abc\n<END>\n" | ./build/pipeline expander,uppercaser,flipper,logger >/dev/null 2>&1'
out="$(wait_for_log_line || true)"
if [[ "${out}" != "C B A" ]]; then
  fail "expander,uppercaser,flipper: expected 'C B A', got '${out}'"
fi
pass "expander→uppercaser→flipper"

# 5) missing plugin must fail
if run_with_timeout ./build/pipeline not_a_plugin >/dev/null 2>&1; then
  fail "missing plugin: pipeline should fail when plugin is not found"
fi
pass "missing plugin"

# 6) sink_stdout prints line and respects <END>
out="$(run_with_timeout sh -c 'printf "ab cd\n<END>\n" | ./build/pipeline sink_stdout' | cat)"
if [[ "${out}" != "ab cd"$'' ]]; then
  fail "sink_stdout: expected 'ab cd', got '${out}'"
fi
pass "sink_stdout basic"

# 7) sink_stdout EOF-only: only <END> produces no output
out="$(run_with_timeout sh -c 'printf "<END>\n" | ./build/pipeline sink_stdout' | cat)"
if [[ -n "${out}" ]]; then
  fail "sink_stdout EOF-only: expected empty output, got '${out}'"
fi
pass "sink_stdout EOF-only"

# 8) missing plugin emits clear error
set +e
err_out=$(run_with_timeout ./build/pipeline this_plugin_should_not_exist 2>&1 >/dev/null)
rc=$?
set -e
if [[ $rc -eq 0 ]]; then
  fail "missing plugin should fail"
fi
if ! echo "$err_out" | grep -E "(dlopen failed|missing required symbols)" >/dev/null; then
  fail "missing plugin error message not clear: $err_out"
fi
pass "missing plugin error message"

# 9) long line (100k chars): ensure non-empty output and safe newline trim
# Generate using Python for portability
long_len=100000
out_len=$(python3 -c "print('a'*${long_len})" 2>/dev/null | run_with_timeout_n 10 ./build/pipeline uppercaser,sink_stdout | wc -c)
if [[ ${out_len} -le 0 ]]; then
  fail "long line: expected non-empty output, got length ${out_len}"
fi
expected_len=$((long_len + 1))
if [[ ${out_len} -ne ${expected_len} ]]; then
  fail "long line: expected byte length ${expected_len} (content + newline), got ${out_len}"
fi
pass "long line (100k)"

# 10) monitor unit test build & run
cc_cmd="${CC:-cc}"
${cc_cmd} -std=c11 -O2 -Wall -Wextra -Werror -pthread tests/monitor_test.c -o build/monitor_test
run_with_timeout ./build/monitor_test >/dev/null 2>&1 || fail "monitor_test failed"
pass "monitor unit test"

# 11) analyzer: uppercaser -> logger basic
EXPECTED="[logger] HELLO"
ACTUAL=$(printf "hello\n<END>\n" | ./output/analyzer 10 uppercaser logger | grep "\[logger\]" | head -n1 || true)
if [[ "$ACTUAL" != "$EXPECTED" ]]; then
  fail "analyzer uppercaser+logger: expected '$EXPECTED', got '$ACTUAL'"
fi
pass "analyzer uppercaser+logger"

# 12) analyzer: empty string
EXPECTED_EMPTY="[logger] "
ACTUAL_EMPTY=$(printf "\n<END>\n" | ./output/analyzer 5 uppercaser logger | grep "\[logger\]" | head -n1 || true)
if [[ "$ACTUAL_EMPTY" != "$EXPECTED_EMPTY" ]]; then
  fail "analyzer empty string: expected '$EXPECTED_EMPTY', got '$ACTUAL_EMPTY'"
fi
pass "analyzer empty string"

# 13) analyzer: invalid args (missing queue size)
set +e
./output/analyzer >/dev/null 2>&1
rc=$?
set -e
if [[ $rc -eq 0 ]]; then
  fail "analyzer invalid args should fail"
fi
pass "analyzer invalid-args"

# 14) analyzer: missing plugin should fail
set +e
err_out=$(./output/analyzer 10 this_plugin_should_not_exist 2>&1 >/dev/null)
rc=$?
set -e
if [[ $rc -eq 0 ]]; then
  fail "analyzer missing plugin should fail"
fi
if ! echo "$err_out" | grep -E "(dlopen failed|missing required symbols)" >/dev/null; then
  fail "analyzer missing plugin error message not clear: $err_out"
fi
pass "analyzer missing plugin error message"

echo "All smoke tests passed."


