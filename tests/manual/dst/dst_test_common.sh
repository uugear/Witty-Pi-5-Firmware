#!/usr/bin/env bash
# Shared register and clock helpers for manual WP5 schedule-script DST tests.
# Sourced by dst_script_prepare.sh, dst_script_check.sh and dst_prepare.sh.
# Not a standalone command. Requires i2c-tools, GNU date and sudo as needed.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
STATE_FILE="${SCRIPT_DIR}/.wp5_dst_test_state"
I2C_BUS="${I2C_BUS:-1}"
I2C_ADDR="${I2C_ADDR:-0x51}"

run_privileged() {
    if (( EUID == 0 )); then
        "$@"
    else
        sudo "$@"
    fi
}

require_tools() {
    local name
    for name in i2cget i2cset date; do
        command -v "$name" >/dev/null 2>&1 || {
            printf 'ERROR: Missing command: %s\n' "$name" >&2
            return 1
        }
    done
}

read_reg() {
    local reg="$1" attempt output
    for ((attempt=1; attempt<=20; attempt++)); do
        output="$(run_privileged i2cget -y "$I2C_BUS" "$I2C_ADDR" "$reg" b 2>/dev/null)" || output=''
        if [[ "$output" =~ ^0[xX][[:xdigit:]]{1,2}$ ]]; then
            printf '%d\n' "$((output))"
            return 0
        fi
        sleep 0.25
    done
    printf 'ERROR: Cannot read WP5 register R%s\n' "$reg" >&2
    return 1
}

write_reg() {
    local reg="$1" value="$2" attempt observed
    for ((attempt=1; attempt<=20; attempt++)); do
        if run_privileged i2cset -y "$I2C_BUS" "$I2C_ADDR" "$reg" "$value" b 2>/dev/null; then
            sleep 0.15
            observed="$(read_reg "$reg")" || observed=-1
            if (( observed == value )); then
                printf 'R%-2s <- 0x%02x  OK\n' "$reg" "$value"
                return 0
            fi
        fi
        sleep 0.3
    done
    printf 'ERROR: Cannot write/verify R%s = 0x%02x\n' "$reg" "$value" >&2
    return 1
}

bcd_encode() {
    local n="$1"
    (( n >= 0 && n <= 99 )) || return 1
    printf '%d\n' "$(( (n / 10) * 16 + n % 10 ))"
}

bcd_decode() {
    local n="$1"
    local high=$(( (n >> 4) & 15 ))
    local low=$(( n & 15 ))
    (( high <= 9 && low <= 9 )) || return 1
    printf '%d\n' "$((high * 10 + low))"
}

# Populate RTC_DATE, RTC_EPOCH and RTC_WEEKDAY using WP5 virtual registers.
read_wp5_rtc() {
    local attempt sec_a sec_b minute hour weekday day month year
    local sec_d min_d hour_d day_d month_d year_d stamp expected_wday
    for ((attempt=1; attempt<=5; attempt++)); do
        sec_a="$(read_reg 80)" || return 1
        minute="$(read_reg 81)" || return 1
        hour="$(read_reg 82)" || return 1
        weekday="$(read_reg 83)" || return 1
        day="$(read_reg 84)" || return 1
        month="$(read_reg 85)" || return 1
        year="$(read_reg 86)" || return 1
        sec_b="$(read_reg 80)" || return 1
        # Retry if the second value wrapped around during the register reads.
        if (( sec_b < sec_a )); then
            continue
        fi
        sec_d="$(bcd_decode "$sec_b")" || return 1
        min_d="$(bcd_decode "$minute")" || return 1
        hour_d="$(bcd_decode "$hour")" || return 1
        day_d="$(bcd_decode "$day")" || return 1
        month_d="$(bcd_decode "$month")" || return 1
        year_d="$(bcd_decode "$year")" || return 1
        printf -v stamp '%04d-%02d-%02d %02d:%02d:%02d' \
            "$((2000 + year_d))" "$month_d" "$day_d" "$hour_d" "$min_d" "$sec_d"
        RTC_EPOCH="$(TZ=UTC date -d "$stamp" +%s 2>/dev/null)" || {
            printf 'ERROR: Invalid WP5 RTC calendar: %s\n' "$stamp" >&2
            return 1
        }
        if [[ "$(TZ=UTC date -d "@$RTC_EPOCH" '+%F %T')" != "$stamp" ]]; then
            printf 'ERROR: Invalid RTC calendar encoding: %s\n' "$stamp" >&2
            return 1
        fi
        RTC_DATE="$stamp"
        RTC_WEEKDAY="$weekday"
        expected_wday="$(TZ=UTC date -d "@$RTC_EPOCH" +%w)"
        if (( weekday != (1 << expected_wday) )); then
            printf 'WARNING: RTC weekday=0x%02x, expected 0x%02x\n' "$weekday" "$((1 << expected_wday))" >&2
        fi
        return 0
    done
    printf 'ERROR: RTC changed during the multi-register read. Retry.\n' >&2
    return 1
}

check_eu_rule() {
    local expected=(3 0 2 0 16 0 3 0) reg value
    # R44 is checked separately, as the script temporarily disables DST.
    for ((reg=45; reg<=52; reg++)); do
        value="$(read_reg "$reg")"
        if (( value != expected[reg - 45] )); then
            printf 'ERROR: R%d=0x%02x; expected 0x%02x (EU rule).\n' \
                "$reg" "$value" "${expected[reg - 45]}" >&2
            return 1
        fi
    done
}
