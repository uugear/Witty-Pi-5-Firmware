#!/usr/bin/env bash
# Clock-setup helper used internally by dst_script_prepare.sh.
# Do not run this helper directly for the schedule-script regression test.
# The parent script feeds PREPARE/NO to configure the experimental RTC without
# manually shutting down Linux; the .act script controls all shutdowns.
# Changes RTC to a 2027 DST boundary and explicitly sets R53.
# Requires Europe/Amsterdam DST configured in WP5 before use.
# An independent clock-only mode exists for legacy tests, but is not part of
# this packaged manual schedule-script test procedure.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/dst_test_common.sh"

usage() {
    printf 'Usage: %s spring|autumn [--enable-log]\n' "$0" >&2
    printf '  spring: 2027-03-28 01:55:00 CET -> 03:00:00 CEST\n' >&2
    printf '  autumn: 2027-10-31 02:55:00 CEST -> 02:00:00 CET\n' >&2
}

(( $# >= 1 && $# <= 2 )) || { usage; exit 2; }
CASE="$1"
ENABLE_LOG=0
if (( $# == 2 )); then
    [[ "$2" == '--enable-log' ]] || { usage; exit 2; }
    ENABLE_LOG=1
fi

case "$CASE" in
    spring)
        TARGET_DATE='2027-03-28'
        TARGET_HOUR=1
        DST_APPLIED=0
        TARGET_LABEL='2027-03-28 01:55:00 CET'
        TRANSITION_LOCAL='2027-03-28 02:00:00'
        TRANSITION_LABEL='2027-03-28 02:00 CET -> 03:00 CEST'
        ;;
    autumn)
        TARGET_DATE='2027-10-31'
        TARGET_HOUR=2
        DST_APPLIED=1
        TARGET_LABEL='2027-10-31 02:55:00 CEST (first occurrence)'
        TRANSITION_LOCAL='2027-10-31 03:00:00'
        TRANSITION_LABEL='2027-10-31 03:00 CEST -> 02:00 CET'
        ;;
    *) usage; exit 2 ;;
esac

# Optional settings for an accelerated alarm test. The clock-only test keeps
# its original 5-minute lead unless the caller explicitly overrides it.
START_CLOCK="${DST_TEST_START_CLOCK:-$(printf '%02d:55:00' "$TARGET_HOUR")}" 
MIN_REMAINING="${DST_TEST_MIN_REMAINING:-120}"
if [[ ! "$START_CLOCK" =~ ^([0-9]{2}):([0-9]{2}):([0-9]{2})$ ]]; then
    echo 'ERROR: Invalid DST_TEST_START_CLOCK.' >&2
    exit 2
fi
TARGET_HOUR=$((10#${BASH_REMATCH[1]}))
TARGET_MINUTE=$((10#${BASH_REMATCH[2]}))
TARGET_SECOND=$((10#${BASH_REMATCH[3]}))
if (( TARGET_HOUR > 23 || TARGET_MINUTE > 59 || TARGET_SECOND > 59 )); then
    echo 'ERROR: Invalid RTC test time.' >&2
    exit 2
fi
if ! [[ "$MIN_REMAINING" =~ ^[0-9]+$ ]]; then
    echo 'ERROR: Invalid DST_TEST_MIN_REMAINING.' >&2
    exit 2
fi
TARGET_LABEL="$TARGET_DATE $START_CLOCK $([[ "$CASE" == spring ]] && echo CET || echo CEST)"
require_tools
printf '=== WP5 DST test preparation: %s ===\n' "$CASE"
printf 'Target RTC: %s\n' "$TARGET_LABEL"
printf 'Transition: %s\n' "$TRANSITION_LABEL"
printf 'Linux clock will NOT be modified.\n\n'

# A normal Linux poweroff must notify WP5 so that the Pi loses power and the
# RP2350 enters hibernation. Without the service this is not that test.
if ! systemctl is-enabled --quiet wp5d_poweroff.service; then
    printf 'ERROR: wp5d_poweroff.service is not enabled.\n' >&2
    printf 'Cannot validate the Pi-OFF/POWMAN hibernation path safely.\n' >&2
    exit 1
fi

rpi_state="$(read_reg 13)"
if (( rpi_state != 2 )); then
    printf 'ERROR: WP5 reports Raspberry Pi state %d, expected ON (2).\n' "$rpi_state" >&2
    exit 1
fi

check_eu_rule
old_r44="$(read_reg 44)"
if (( old_r44 != 0 && old_r44 != 60 )); then
    printf 'ERROR: R44=0x%02x; expected 0x3c (60 min DST).\n' "$old_r44" >&2
    exit 1
fi
old_r53="$(read_reg 53)"
old_r30="$(read_reg 30)"
read_wp5_rtc
printf 'Original WP5 RTC: %s\n' "$RTC_DATE"
printf 'Original R44=0x%02x R53=%d LOG_TO_FILE=%d\n' \
    "$old_r44" "$old_r53" "$old_r30"

script_flag="$(read_reg 15)"
if (( (script_flag & 1) != 0 )); then
    printf '\nWARNING: WP5 currently has an active schedule script.\n'
    printf 'It may cause an automatic Pi wake or shutdown during this clock-only test.\n'
fi

if (( ENABLE_LOG == 0 && old_r30 == 0 )); then
    printf '\nWARNING: LOG_TO_FILE=0. The RTC result can be checked, but\n'
    printf 'a post-boot check alone does NOT prove the special POWMAN wake fired.\n'
    printf 'For stronger evidence, rerun with --enable-log.\n'
fi

printf '\nThis test deliberately jumps the WP5 RTC into 2027.\n'
printf 'Keep WP5 itself powered throughout the entire test.\n'
read -r -p 'Type PREPARE to change the RTC (anything else cancels): ' confirmation
[[ "$confirmation" == 'PREPARE' ]] || { printf 'Cancelled.\n'; exit 1; }

# Disable automatic DST adjustment until the RTC and applied state agree.
write_reg 44 0
if (( ENABLE_LOG == 1 && old_r30 != 1 )); then
    write_reg 30 1
fi

year="${TARGET_DATE:2:2}"
month="${TARGET_DATE:5:2}"
day="${TARGET_DATE:8:2}"
weekday="$(TZ=UTC date -d "$TARGET_DATE" +%w)"

# Set seconds last to anchor the test time; WP5 registers are BCD, and
# the RTC weekday register is a one-hot bit mask, not a BCD weekday value.
write_reg 86 "$(bcd_encode "$((10#$year))")"
write_reg 85 "$(bcd_encode "$((10#$month))")"
write_reg 84 "$(bcd_encode "$((10#$day))")"
write_reg 83 "$((1 << weekday))"
write_reg 82 "$(bcd_encode "$TARGET_HOUR")"
write_reg 81 "$(bcd_encode "$TARGET_MINUTE")"
write_reg 80 "$(bcd_encode "$TARGET_SECOND")"

# In autumn, setting R53 before the RTC while DST is enabled would allow
# the firmware's configuration callback to undo the requested phase.
write_reg 53 "$DST_APPLIED"
write_reg 44 60

# Allow delayed RTC-to-POWMAN synchronization to complete.
sleep 1
read_wp5_rtc
r53="$(read_reg 53)"
r44="$(read_reg 44)"
printf '\nNew WP5 RTC: %s\n' "$RTC_DATE"
printf 'R44=0x%02x R53=%d\n' "$r44" "$r53"
if (( r44 != 60 || r53 != DST_APPLIED )); then
    printf 'ERROR: DST configuration was changed unexpectedly. Do not power off.\n' >&2
    exit 1
fi
if [[ "${RTC_DATE:0:16}" != "${TARGET_DATE} ${START_CLOCK:0:5}" ]]; then
    printf 'ERROR: RTC readback differs from the requested starting time.\n' >&2
    exit 1
fi

umask 077
{
    printf 'CASE=%q\n' "$CASE"
    printf 'TARGET_DATE=%q\n' "$TARGET_DATE"
    printf 'TARGET_LABEL=%q\n' "$TARGET_LABEL"
    printf 'TRANSITION_LABEL=%q\n' "$TRANSITION_LABEL"
    printf 'OLD_R44=%q\n' "$old_r44"
    printf 'OLD_R53=%q\n' "$old_r53"
    printf 'OLD_R30=%q\n' "$old_r30"
    printf 'ENABLE_LOG=%q\n' "$ENABLE_LOG"
    printf 'START_RTC=%q\n' "$RTC_DATE"
} > "$STATE_FILE"

printf '\nPreparation complete.\n'
printf 'Shut the Pi down now; wait for WP5 to cut Pi power.\n'
printf 'Do not disconnect power from WP5. Wait until AFTER the transition,\n'
printf 'then press the WP5 button to start the Pi again.\n'
printf 'After login, run: ./dst_check.sh\n'
printf '\n'
read -r -p 'Type OFF to execute sudo shutdown -h now: ' confirmation
if [[ "$confirmation" == 'OFF' ]]; then
    # Refuse a late shutdown: the DST transition must happen with Pi power off.
    read_wp5_rtc
    current_r53="$(read_reg 53)"
    transition_epoch="$(TZ=UTC date -d "$TRANSITION_LOCAL" +%s)"
    remaining=$((transition_epoch - RTC_EPOCH))
    if (( current_r53 != DST_APPLIED || remaining < MIN_REMAINING )); then
        printf 'ERROR: Only %d seconds remain or DST has already switched.\n' "$remaining" >&2
        printf 'Do not shut down now. Rerun the preparation script.\n' >&2
        exit 1
    fi
    printf 'Approx. %d seconds remain before DST transition.\n' "$remaining"
    run_privileged shutdown -h now
else
    printf 'Shutdown cancelled. The test RTC remains set; run sudo shutdown -h now when ready.\n'
fi
