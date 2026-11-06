#!/usr/bin/env bash
# DESTRUCTIVE manual hardware DST regression test: schedule-script path.
# Usage on a test Raspberry Pi (from this directory):
#   ./dst_script_prepare.sh spring
#   ./dst_script_prepare.sh autumn
# Prerequisites: WP5 firmware with DST support, wp5 CLI 5.3.x, enabled
# wp5d_poweroff.service, i2c-tools, GNU date, systemctl, timeout, python3,
# and sudo access for I2C operations. Raspberry Pi must be powered via WP5.
# Preconfigure Europe/Amsterdam DST: R44=0x3c and R45..R52 =
#   03 00 02 00 10 00 03 00 (hex byte values).
# WARNING: Changes WP5 RTC to 2027, replaces the ACTIVE schedule.* files,
# overwrites alarm registers R32..R39, and enables firmware file logging.
# Do NOT use on a unit with an active schedule you need to keep.
# The .act schedule triggers EVERY planned shutdown and startup; this shell
# script must NOT call shutdown. KEEP POWER CONNECTED TO WP5 throughout.
# Spring (~8 min): DN 01:59 CET, DST jump to 03:00 CEST,
#   UP 03:02 CEST, DN 03:05 CEST.
# Autumn (~73 min): DN 02:54 CEST, UP 02:56, DN 02:58;
#   rollback 03:00 CEST -> 02:00 CET; NO replay at second 02:56 CET;
#   UP 03:02 CET, DN 03:05 CET.
# DO NOT press the power button after the first shutdown, or after any
# intermediate shutdown. Press it ONLY AFTER the final scheduled shutdown.
# Then run ./dst_script_check.sh final, followed by cleanup.
# Restore the REAL RTC time, DST applied state, and NTP settings manually.
# See dst_script_check.sh for validation and cleanup instructions.
set -euo pipefail
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/dst_test_common.sh"
STATE="$HERE/.wp5_dst_script_state"
PREP_LOG="$HERE/dst_script_clock_setup.log"
UPLOAD_LOG="$HERE/dst_script_upload.log"
LIST_LOG="$HERE/dst_script_remote_list.log"
ACTIVATE_LOG="$HERE/dst_script_activation.log"

usage() { echo "Usage: $0 spring|autumn" >&2; exit 2; }
(( $# == 1 )) || usage
CASE="$1"
case "$CASE" in
  spring)
    TEST_FILE='dst_lab_spring.act'
    START_DATE='2027-03-28'
    START_CLOCK='01:57:00'
    NEXT_SHUT='01:59:00'
    NEXT_BOOT='03:02:00'
    NEXT_OFF='03:05:00'
    START_PHASE=0
    DURATION='About 8 minutes from the RTC setup; final check after 03:05 CEST.'
    ;;
  autumn)
    TEST_FILE='dst_lab_autumn.act'
    START_DATE='2027-10-31'
    START_CLOCK='02:52:00'
    NEXT_SHUT='02:54:00'
    NEXT_BOOT='02:56:00'
    NEXT_OFF='02:58:00'
    START_PHASE=1
    DURATION='About 73 minutes from the RTC setup; final check after 03:05 CET.'
    ;;
  *) usage ;;
esac

require_tools
for program in wp5 timeout python3 systemctl; do
  command -v "$program" >/dev/null || { echo "Missing command: $program" >&2; exit 1; }
done
[[ ! -e "$HERE/.wp5_dst_alarm_state" ]] || { echo 'An unfinished ordinary Alarm test state exists. Complete its cleanup first.' >&2; exit 1; }
[[ ! -e "$STATE" ]] || { echo "Previous script-test state exists. Run dst_script_check.sh cleanup first." >&2; exit 1; }
(( $(read_reg 13) == 2 )) || { echo 'RPi must currently be ON.' >&2; exit 1; }
check_eu_rule
(( $(read_reg 44) == 60 )) || { echo 'EU DST rules must be enabled (R44=0x3c).' >&2; exit 1; }

printf '\n=== Actual schedule-script DST test: %s ===\n' "$CASE"
printf 'Initial RTC: %s %s\n' "$START_DATE" "$START_CLOCK"
printf '%s\n' "$DURATION"
if [[ "$CASE" == spring ]]; then
  cat <<'PLAN'
  Automatic script shutdown: 01:59 CET
  Nonexistent UP 02:02 CET -> expected startup 03:02 CEST
  Nonexistent DN 02:05 CET -> expected shutdown 03:05 CEST
PLAN
else
  cat <<'PLAN'
  First shutdown: 02:54 CEST
  First startup:  02:56 CEST
  First shutdown: 02:58 CEST
  DST rollback:   03:00 CEST -> 02:00 CET
  Repeated 02:56 CET MUST NOT startup again
  Next startup:   03:02 CET
  Next shutdown:  03:05 CET
PLAN
fi
printf '\nExisting active-script status R15=0x%02x\n' "$(read_reg 15)"
printf 'Existing alarm registers:'
for r in {32..39}; do printf ' R%d=%02x' "$r" "$(read_reg "$r")"; done
printf '\n\nActivating a new script REPLACES the active schedule.* files on WP5.\n'
printf 'Do not use this on a WP5 with a schedule you need to preserve.\n'
printf 'A copy of the test file will be uploaded; neither RTC nor R53 is restored at end.\n'
read -r -p 'Type REPLACE to proceed: ' confirm
[[ "$confirm" == 'REPLACE' ]] || { echo 'Cancelled.'; exit 1; }

# Eliminate any pre-existing script before a simulated RTC jump.
if (( ($(read_reg 15) & 1) != 0 )); then
  if ! printf '12\n3\n14\n' | timeout 60s wp5 > "$HERE/dst_script_purge_previous.log" 2>&1 ||
      ! grep -q 'Schedule script is not being used now!' "$HERE/dst_script_purge_previous.log"; then
    echo 'Could not deactivate the previous script. Abort.' >&2
    exit 1
  fi
fi

# Start from a clean conventional-alarm state, so any action is from the schedule.
for reg in {32..39}; do
  if (( $(read_reg "$reg") != 0 )); then write_reg "$reg" 0; fi
done

printf '\nSetting WP5 RTC and DST state...\n'
if ! printf 'PREPARE\nNO\n' |
    env DST_TEST_START_CLOCK="$START_CLOCK" DST_TEST_MIN_REMAINING=90 \
        "$HERE/dst_prepare.sh" "$CASE" --enable-log > "$PREP_LOG" 2>&1; then
  cat "$PREP_LOG" >&2
  echo 'RTC preparation failed. Do not start the test.' >&2
  exit 1
fi
grep -E 'New WP5 RTC:|R44=|Preparation complete' "$PREP_LOG" || true

# Use source .act rather than hand-generating a .skd numeric timestamp.
if [[ "$CASE" == spring ]]; then
  cat > "$HERE/$TEST_FILE" <<'ACT'
DN 2027-03-28 01:59:00
UP 2027-03-28 02:02:00
DN 2027-03-28 02:05:00
UP 2027-03-29 03:30:00
DN 2027-03-29 03:32:00
ACT
else
  cat > "$HERE/$TEST_FILE" <<'ACT'
DN 2027-10-31 02:54:00
UP 2027-10-31 02:56:00
DN 2027-10-31 02:58:00
UP 2027-10-31 03:02:00
DN 2027-10-31 03:05:00
UP 2027-11-01 03:30:00
DN 2027-11-01 03:32:00
ACT
fi

# Record state before performing the potentially destructive activation.
umask 077
{
  printf 'CASE=%q\n' "$CASE"
  printf 'TEST_FILE=%q\n' "$TEST_FILE"
  printf 'EXPECTED_START_PHASE=%q\n' "$START_PHASE"
} > "$STATE"

# Drive the wp5 5.3.x menus by stdin; inspect output rather than exit status only.
cd "$HERE"
printf '\nUploading %s over WP5 I2C administration...\n' "$TEST_FILE"
if ! printf '6\n1\n%s\n5\n14\n' "$TEST_FILE" | timeout 90s wp5 > "$UPLOAD_LOG" 2>&1 ||
   ! grep -Fq "Uploaded $TEST_FILE to Witty Pi." "$UPLOAD_LOG"; then
  tail -50 "$UPLOAD_LOG" >&2
  echo 'Upload failed; inspect dst_script_upload.log. Do not assume the script was activated.' >&2
  exit 1
fi

# Obtain the current on-device list; do not assume the test file is index 1.
if ! printf '6\n2\n\n5\n14\n' | timeout 60s wp5 > "$LIST_LOG" 2>&1; then
  echo 'Failed to list remote scripts.' >&2
  exit 1
fi
idx="$(awk -v wanted="$TEST_FILE" '
  /Available files for activation:/ {in_list=1; next}
  in_list && /Please choose a file:/ {in_list=0}
  in_list && /^[[:space:]]*\[[0-9]+\][[:space:]]/ {
    line=$0
    sub(/^[[:space:]]*\[/,"",line)
    split(line,a,"]")
    n=a[1]
    sub(/^[[:space:]]*\[[0-9]+\][[:space:]]*/,"",$0)
    if ($0 == wanted) print n
  }
' "$LIST_LOG" | head -1)"
if [[ ! "$idx" =~ ^[1-9][0-9]*$ ]]; then
  tail -50 "$LIST_LOG" >&2
  echo "Could not identify $TEST_FILE in the remote file list." >&2
  exit 1
fi
printf 'Remote file index: %s\n' "$idx"

if ! printf '6\n2\n%s\n5\n14\n' "$idx" | timeout 120s wp5 > "$ACTIVATE_LOG" 2>&1 ||
   ! grep -Fq "Activating $TEST_FILE" "$ACTIVATE_LOG" ||
   ! grep -q 'Scheduled shutdown:' "$ACTIVATE_LOG"; then
  tail -65 "$ACTIVATE_LOG" >&2
  echo 'Activation not confirmed. Abort; inspect the menu and firmware logs.' >&2
  exit 1
fi

sleep 0.4
read_wp5_rtc
r15="$(read_reg 15)"
r53="$(read_reg 53)"
r44="$(read_reg 44)"
printf '\nActivation result: WP5 RTC %s, R15=0x%02x, R53=%d\n' "$RTC_DATE" "$r15" "$r53"
if (( (r15 & 1) == 0 || r44 != 60 || r53 != START_PHASE )); then
  echo 'Active script or DST state mismatch. Do not rely on test results.' >&2
  exit 1
fi

# A real schedule, not the shell, should perform the first shutdown.
initial_shutdown_epoch="$(TZ=UTC date -d "$START_DATE $NEXT_SHUT" +%s)"
seconds_left=$(( initial_shutdown_epoch - RTC_EPOCH ))
if (( seconds_left < 45 )); then
  echo 'Not enough time until the first script shutdown; test was not safely armed.' >&2
  echo 'Use dst_script_check.sh cleanup and retry.' >&2
  exit 1
fi

echo
printf 'PASS: schedule.act activated; first scheduled DN is due in %d seconds.\n' "$seconds_left"
printf 'DO NOT manually run shutdown, press the WP5 button, or use wp5 menu to modify alarms.\n'
printf 'After script-controlled final shutdown, press the button once and run:\n'
printf '  ./dst_script_check.sh final\n'
printf 'To inspect without changing anything while the Pi is ON:\n'
printf '  ./dst_script_check.sh status\n'
printf 'To cancel an unfinished test (Pi must be ON):\n'
printf '  ./dst_script_check.sh cleanup\n'
