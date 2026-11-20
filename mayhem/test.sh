#!/usr/bin/env bash
# mayhem/test.sh — run the DS5Dongle known-answer self-test (built by mayhem/build.sh with normal
# flags). It drives the real firmware sources through the USB and Bluetooth entry points and checks
# concrete outputs: the 0xF8 version report, a 0xF6/0xF7 config round-trip persisted through flash,
# an output report 0x02 forwarded to the controller as BT report 0x31 with a correct CRC-32, a BT
# feature report served back over USB GET_REPORT, and a BT input report reaching the USB IN report.
# Upstream's tests/test_hold.py compiles its test at run time, so it is not run here.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

EXPECTED=5
RUNNER="$SRC/mayhem/bin/ds5_selftest"
if [ ! -x "$RUNNER" ]; then
  echo "test.sh: $RUNNER missing — run mayhem/build.sh first" >&2
  emit_ctrf "ds5-selftest" 0 "$EXPECTED" 0
  exit 1
fi
out="$("$RUNNER" 2>&1)"; rc=$?
printf '%s\n' "$out" | grep -E '^(PASS|FAIL|SUMMARY) '
passed=$(printf '%s\n' "$out" | grep -c '^PASS ')
failed=$(printf '%s\n' "$out" | grep -c '^FAIL ')
# A case that never reported (runner died or exited early) counts as failed.
missing=$(( EXPECTED - passed - failed )); [ "$missing" -gt 0 ] && failed=$(( failed + missing ))
[ "$rc" -ne 0 ] && [ "$failed" -eq 0 ] && failed=1
emit_ctrf "ds5-selftest" "$passed" "$failed" 0
