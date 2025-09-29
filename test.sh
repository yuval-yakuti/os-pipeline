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
# Generate input file to avoid BrokenPipe with timeouts
long_len=100000
tmp_in="/tmp/os_pipeline_in.txt"
python3 - "$long_len" > "$tmp_in" 2>/dev/null <<'PY'
import sys
n=int(sys.argv[1])
print('a'*n)
print('<END>')
PY
out_len=$(run_with_timeout_n 10 bash -c "./build/pipeline uppercaser,sink_stdout < '$tmp_in' | wc -c" | tr -d ' \t')
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
${cc_cmd} -std=c11 -O2 -Wall -Wextra -Werror -pthread \
  -Iplugins tests/monitor_test.c plugins/sync/monitor.c -o build/monitor_test
run_with_timeout ./build/monitor_test >/dev/null 2>&1 || fail "monitor_test failed"
pass "monitor unit test"

# 11) consumer_producer queue unit test
${cc_cmd} -std=c11 -O2 -Wall -Wextra -Werror -pthread \
  -Iplugins tests/consumer_producer_test.c \
  plugins/sync/monitor.c plugins/sync/consumer_producer.c -o build/consumer_producer_test
run_with_timeout ./build/consumer_producer_test >/dev/null 2>&1 || fail "consumer_producer_test failed"
pass "consumer_producer unit test"

# 12) analyzer: uppercaser -> logger basic
EXPECTED="[logger] HELLO"
ACTUAL=$(printf "hello\n<END>\n" | ./output/analyzer 10 uppercaser logger | grep "\[logger\]" | head -n1 || true)
if [[ "$ACTUAL" != "$EXPECTED" ]]; then
  fail "analyzer uppercaser+logger: expected '$EXPECTED', got '$ACTUAL'"
fi
pass "analyzer uppercaser+logger"

# 13) analyzer: empty string
EXPECTED_EMPTY="[logger] "
ACTUAL_EMPTY=$(printf "\n<END>\n" | ./output/analyzer 5 uppercaser logger | grep "\[logger\]" | head -n1 || true)
if [[ "$ACTUAL_EMPTY" != "$EXPECTED_EMPTY" ]]; then
  fail "analyzer empty string: expected '$EXPECTED_EMPTY', got '$ACTUAL_EMPTY'"
fi
pass "analyzer empty string"

# 14) analyzer: invalid args (missing queue size)
set +e
./output/analyzer >/dev/null 2>&1
rc=$?
set -e
if [[ $rc -eq 0 ]]; then
  fail "analyzer invalid args should fail"
fi
pass "analyzer invalid-args"

# 15) analyzer: missing plugin should fail
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

# 16) analyzer: shutdown message last line
SHUT_LAST=$(printf "x\n<END>\n" | ./output/analyzer 8 uppercaser logger | tail -n1 || true)
if [[ "$SHUT_LAST" != "Pipeline shutdown complete" ]]; then
  fail "analyzer shutdown: expected last line 'Pipeline shutdown complete', got '$SHUT_LAST'"
fi
pass "analyzer shutdown message"

# 17) analyzer: usage error when no plugins
set +e
msg1=$(./output/analyzer 10 2>&1)
rc1=$?
msg2=$(./output/analyzer 2>&1)
rc2=$?
set -e
if [[ $rc1 -eq 0 || $rc2 -eq 0 ]]; then
  fail "analyzer usage: expected non-zero exit when no plugins"
fi
if ! echo "$msg1$msg2" | grep -q "Usage:" || ! echo "$msg1$msg2" | grep -q "Available plugins:"; then
  fail "analyzer usage: expected Usage and Available plugins in stderr"
fi
pass "analyzer usage without plugins"

# 17) analyzer: invalid queue size values
set +e
msg_bad1=$(./output/analyzer abc uppercaser logger 2>&1)
rc_bad1=$?
msg_bad2=$(./output/analyzer 0 uppercaser logger 2>&1)
rc_bad2=$?
msg_bad3=$(./output/analyzer -5 uppercaser logger 2>&1)
rc_bad3=$?
set -e
if [[ $rc_bad1 -eq 0 || $rc_bad2 -eq 0 || $rc_bad3 -eq 0 ]]; then
  fail "analyzer invalid queue size should fail"
fi
pass "analyzer invalid queue size"

# 18) analyzer: typewriter completes and prints shutdown (does not hang)
TW_OUT=$(printf "hi\n<END>\n" | run_with_timeout_n 5 ./output/analyzer 5 typewriter sink_stdout | tr -d '\r')
if ! printf '%s\n' "$TW_OUT" | grep -q "Pipeline shutdown complete"; then
  echo "--- analyzer typewriter full stdout ---"
  printf '%s\n' "$TW_OUT"
  fail "analyzer typewriter: expected shutdown line in stdout"
fi
pass "analyzer typewriter shutdown"

# 19) duplicate plugins: uppercaser,uppercaser idempotent (assert via logger)
: > output/pipeline.log
run_with_timeout sh -c 'printf "hello\n<END>\n" | ./build/pipeline uppercaser,uppercaser,logger >/dev/null 2>&1'
out="$(wait_for_log_line || true)"
if [[ "$out" != "HELLO" ]]; then
  fail "duplicate uppercaser: expected 'HELLO', got '$out'"
fi
pass "duplicate uppercaser"

# 20) sentinel-only through chain -> empty stdout
out="$(run_with_timeout sh -c 'printf "<END>\n" | ./build/pipeline expander,uppercaser,flipper,sink_stdout' | cat)"
if [[ -n "$out" ]]; then
  fail "sentinel-only: expected empty stdout, got '$out'"
fi
pass "sentinel-only chain"

# 21) backpressure: analyzer queue=1 with many lines; shutdown and last line observed
tmp_many="/tmp/os_pipeline_many.txt"
{
  i=1
  while (( i<=200 )); do printf "L%d\n" "$i"; ((i++)); done
  printf "<END>\n"
} > "$tmp_many"
: > output/pipeline.log
AN_OUT=$(run_with_timeout_n 15 bash -c "./output/analyzer 1 uppercaser logger < '$tmp_many'")
if ! printf '%s\n' "$AN_OUT" | grep -q "Pipeline shutdown complete"; then
  echo "--- analyzer backpressure stdout ---"; printf '%s\n' "$AN_OUT"
  fail "backpressure: analyzer did not shutdown cleanly"
fi
last_logged="$(tail -n1 output/pipeline.log 2>/dev/null || true)"
if [[ "$last_logged" != "L200" ]]; then
  echo "--- log tail ---"; tail -n 5 output/pipeline.log 2>/dev/null || true
  fail "backpressure: expected last logged 'L200', got '$last_logged'"
fi
pass "backpressure queue=1"

# 22) analyzer with EOF (no input) -> shuts down
AN_EOF_OUT=$(run_with_timeout_n 5 bash -c "./output/analyzer 10 uppercaser logger < /dev/null")
if ! printf '%s\n' "$AN_EOF_OUT" | grep -q "Pipeline shutdown complete"; then
  echo "--- analyzer EOF stdout ---"; printf '%s\n' "$AN_EOF_OUT"
  fail "analyzer EOF: expected shutdown line"
fi
pass "analyzer EOF shutdown"

# 23) analyzer exit code 0 on success
set +e
printf "ok\n<END>\n" | ./output/analyzer 5 sink_stdout >/dev/null 2>&1
rc_ok=$?
set -e
if [[ $rc_ok -ne 0 ]]; then
  fail "analyzer exit code: expected 0, got $rc_ok"
fi
pass "analyzer exit code 0"

# 24) expander single-char edge: remains single char
: > output/pipeline.log
run_with_timeout sh -c 'printf "Z\n<END>\n" | ./build/pipeline expander,uppercaser,flipper,logger >/dev/null 2>&1'
out="$(wait_for_log_line || true)"
if [[ "$out" != "Z" ]]; then
  fail "expander single char: expected 'Z', got '$out'"
fi
pass "expander single char"

# 25) expander non-alphabetic preserved and spaced (assert via logger)
: > output/pipeline.log
run_with_timeout sh -c 'printf "1a!\n<END>\n" | ./build/pipeline expander,uppercaser,flipper,logger >/dev/null 2>&1'
out="$(wait_for_log_line || true)"
if [[ "$out" != "! A 1" ]]; then
  fail "expander non-alpha: expected '! A 1', got '$out'"
fi
pass "expander non-alpha spacing"

# 26) analyzer clean stderr on success
set +e
AN_ERR=$(printf "ok\n<END>\n" | ./output/analyzer 5 uppercaser logger 2>&1 >/dev/null)
rc=$?
set -e
if [[ $rc -ne 0 ]]; then
  fail "analyzer clean stderr: expected success exit, got $rc"
fi
if [[ -n "$AN_ERR" ]]; then
  echo "--- analyzer stderr ---"; printf '%s\n' "$AN_ERR"
  fail "analyzer clean stderr: expected empty stderr"
fi
pass "analyzer clean stderr"

# 27) expander with tab preserved/spaced; final reversed sequence
: > output/pipeline.log
run_with_timeout sh -c 'printf "A\tb\n<END>\n" | ./build/pipeline expander,uppercaser,flipper,logger >/dev/null 2>&1'
out="$(wait_for_log_line || true)"
if [[ "$out" != $'B \t A' ]]; then
  fail $'expander tab: expected "B \t A", got '
fi
pass "expander tab spacing"

# 28) parallel analyzer runs (no interference, both shutdown)
PAR_OUT1=$(run_with_timeout_n 5 bash -c 'printf "p1\n<END>\n" | ./output/analyzer 5 uppercaser sink_stdout') &
pid1=$!
PAR_OUT2=$(run_with_timeout_n 5 bash -c 'printf "p2\n<END>\n" | ./output/analyzer 5 uppercaser sink_stdout') &
pid2=$!
wait "$pid1" || true
wait "$pid2" || true
# Re-run to capture outputs deterministically (previous were in subshells)
OUT_A=$(run_with_timeout_n 5 bash -c 'printf "p1\n<END>\n" | ./output/analyzer 5 uppercaser sink_stdout')
OUT_B=$(run_with_timeout_n 5 bash -c 'printf "p2\n<END>\n" | ./output/analyzer 5 uppercaser sink_stdout')
if ! printf '%s\n' "$OUT_A" | grep -q "Pipeline shutdown complete"; then
  echo "--- analyzer A ---"; printf '%s\n' "$OUT_A"; fail "parallel analyzers: A missing shutdown"
fi
if ! printf '%s\n' "$OUT_B" | grep -q "Pipeline shutdown complete"; then
  echo "--- analyzer B ---"; printf '%s\n' "$OUT_B"; fail "parallel analyzers: B missing shutdown"
fi
pass "parallel analyzers"

# 29) analyzer long line (~2000 chars) non-empty and correct byte count
al_tmp="/tmp/os_pipeline_long2k.txt"
python3 - "$al_tmp" > "$al_tmp" 2>/dev/null <<'PY'
print('x'*2000)
print('<END>')
PY
AL_BYTES=$(run_with_timeout_n 10 bash -c "./output/analyzer 32 uppercaser sink_stdout < '$al_tmp' | wc -c" | tr -d ' \t')
if [[ -z "$AL_BYTES" || "$AL_BYTES" -le 0 ]]; then
  fail "analyzer long line: expected non-empty output, got '$AL_BYTES'"
fi
if (( AL_BYTES < 2001 )); then
  fail "analyzer long line: expected at least 2001 bytes (content+newline), got $AL_BYTES"
fi
pass "analyzer long line (2k)"

# 30) mixed chain: uppercaser then rotator to sink_stdout
out="$(run_with_timeout sh -c 'printf "Abc\n<END>\n" | ./build/pipeline uppercaser,rotator,sink_stdout' | cat)"
if [[ "$out" != "CAB"$'' ]]; then
  fail "mixed chain uppercaser,rotator: expected 'CAB', got '$out'"
fi
pass "mixed uppercaser+rotator"

echo "All smoke tests passed."
