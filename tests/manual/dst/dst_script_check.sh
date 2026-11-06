#!/usr/bin/env bash
# Inspect, verify, and clean up a manual schedule-script DST test.
# Usage (from this directory, after dst_script_prepare.sh spring|autumn):
#   ./dst_script_check.sh status   # Read-only observation while Pi is on
#   ./dst_script_check.sh final    # AFTER final script shutdown + manual boot
#   ./dst_script_check.sh analyze  # Reanalyze previously downloaded log
#   ./dst_script_check.sh cleanup  # PURGE active schedule and clear R32..R39
# IMPORTANT: Do NOT press the button before the final scheduled shutdown.
# The final command downloads WP5's firmware log using wp5's Admin menu.
# Cleanup is DESTRUCTIVE and does not restore original scripts, original RTC,
# RTC/DST state, network time sync settings, or unused uploaded .act files.
# Always perform cleanup promptly to prevent next-day scheduled actions.
# The generated firmware log and helper outputs remain as ignored local files.
# Requires wp5 CLI 5.3.x, python3, i2c-tools and sudo access on Raspberry Pi.
set -euo pipefail
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/dst_test_common.sh"
STATE="$HERE/.wp5_dst_script_state"
usage() { echo "Usage: $0 status|final|analyze|cleanup" >&2; exit 2; }
(( $# == 1 )) || usage
MODE="$1"
case "$MODE" in status|final|analyze|cleanup) ;; *) usage ;; esac
[[ -f "$STATE" ]] || { echo 'No schedule-script test state. Run dst_script_prepare.sh first.' >&2; exit 1; }
source "$STATE"
require_tools

show_state() {
  read_wp5_rtc
  printf 'Case=%s | WP5 RTC=%s | R53=%d | R15=0x%02x | R13=%d | R14=0x%02x\n' \
    "$CASE" "$RTC_DATE" "$(read_reg 53)" "$(read_reg 15)" "$(read_reg 13)" "$(read_reg 14)"
  printf 'Configured alarm values (BCD):'
  for reg in {32..39}; do printf ' R%d=%02x' "$reg" "$(read_reg "$reg")"; done
  printf '\n'
}

if [[ "$MODE" == status ]]; then
  show_state
  echo 'This is observation only: R14 may identify the most recent startup, but a full result needs firmware logs.'
  exit 0
fi

if [[ "$MODE" == cleanup ]]; then
  echo 'This will PURGE active schedule.wpi/.act/.skd from WP5, and CLEAR R32..R39.'
  echo "It does not restore the RTC, R44/R53, or the previously selected schedule."
  read -r -p 'Type PURGE to continue: ' confirm
  [[ "$confirm" == PURGE ]] || { echo 'Cancelled.'; exit 1; }
  if ! printf '12\n3\n14\n' | timeout 60s wp5 > "$HERE/dst_script_cleanup.log" 2>&1 ||
     ! grep -q 'Schedule script is not being used now!' "$HERE/dst_script_cleanup.log"; then
    echo 'Failed to confirm script purge. See dst_script_cleanup.log' >&2
    exit 1
  fi
  for reg in {32..39}; do write_reg "$reg" 0; done
  if (( ($(read_reg 15) & 1) != 0 )); then
    echo 'Script still appears active. Investigate before returning to production.' >&2
    exit 1
  fi
  rm -f "$STATE"
  echo 'Active test schedule purged, alarms cleared.'
  echo "Uploaded named source file $TEST_FILE may remain on WP5; delete through wp5 menu 6 -> 4 if desired."
  echo 'RTC still contains experimental 2027 date; restore time and DST state separately.'
  exit 0
fi

LOG="$HERE/WittyPi5.log"
if [[ "$MODE" == final ]]; then
  show_state
  echo 'Only run final after the LAST script-controlled shutdown and a manual restart.'
  read -r -p 'Type DOWNLOAD to retrieve the firmware log: ' confirm
  [[ "$confirm" == DOWNLOAD ]] || { echo 'Cancelled.'; exit 1; }
  if [[ -f "$LOG" ]]; then
    mv -- "$LOG" "$HERE/WittyPi5.saved.$(date +%s).log"
  fi
  # 13=Admin, 6=Save log, 7=Download log, 9=Return, 14=Exit.
  if ! (cd "$HERE"; printf '13\n6\n7\n9\n14\n' | timeout 240s wp5 > "$HERE/dst_script_download.log" 2>&1) ||
      [[ ! -s "$LOG" ]]; then
    tail -55 "$HERE/dst_script_download.log" >&2
    echo 'Automatic log download failed. Use wp5 Admin 13 -> 6 then 13 -> 7.' >&2
    exit 1
  fi
fi

if [[ ! -s "$LOG" ]]; then
  echo "No $LOG: run final to download, or copy a complete firmware log to that path." >&2
  exit 1
fi
printf '\nAnalyzing firmware log...\n'
python3 "$HERE/dst_script_analyze.py" --case "$CASE" --log "$LOG"
echo
echo 'Evidence is saved locally. Run dst_script_check.sh cleanup when finished.'
