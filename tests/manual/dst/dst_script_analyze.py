#!/usr/bin/env python3
"""Analyze an isolated WP5 DST schedule-script firmware log.

Usage (offline; no hardware needed):
    python3 dst_script_analyze.py --case spring --log WittyPi5.log
    python3 dst_script_analyze.py --case autumn --log WittyPi5.log

The log must contain the complete test run, including script activation.
Timestamps in this test fixture are intentionally anchored to 2027.
Spring checks DST gap handling; autumn checks the repeated 02:xx hour.
"""
import argparse
from datetime import datetime
import re
import sys
from pathlib import Path

LINE = re.compile(r'^\[(\d\d-\d\d) (\d\d:\d\d:\d\d)\.\d+\] (.*)$')


def parse_log(path):
    events = []
    for raw in path.read_text(encoding='utf-8', errors='replace').splitlines():
        match = LINE.match(raw)
        if match:
            mmdd, clock, message = match.groups()
            try:
                moment = datetime.strptime('2027-' + mmdd + ' ' + clock, '%Y-%m-%d %H:%M:%S')
            except ValueError:
                continue
            events.append((moment, message, raw))
    return events


def analyze(path, case):
    events = parse_log(path)
    filename = 'dst_lab_spring.act' if case == 'spring' else 'dst_lab_autumn.act'
    anchors = [i for i, (_, message, _) in enumerate(events)
               if f'Applying script {filename}' in message]
    if not anchors:
        print(f'INCONCLUSIVE: No "Applying script {filename}" in log.')
        print('Download a fresh full firmware log, and verify the script was activated.')
        return 2
    anchor = anchors[-1]
    run = events[anchor:]
    print(f'=== {case} actual schedule test ===')
    print(f'Log: {path} | anchor: {run[0][2]}')
    issues = []

    def locate(after, text, clock=None, tolerance=7, before=None):
        for i in range(after, len(run)):
            if before is not None and i >= before:
                break
            dt, message, _ = run[i]
            if text not in message:
                continue
            if clock is not None:
                expected = datetime.strptime('2027-' + ('03-28' if case == 'spring' else '10-31') + ' ' + clock,
                                             '%Y-%m-%d %H:%M:%S')
                if abs((dt - expected).total_seconds()) > tolerance:
                    continue
            return i
        return -1

    def required(after, text, clock=None, tolerance=7):
        index = locate(after, text, clock, tolerance)
        label = f'{text} @ {clock}' if clock else text
        if index < 0:
            issues.append('Missing or out-of-order: ' + label)
            print('FAIL:', label)
        else:
            print('PASS:', run[index][2])
        return index

    def next_after(i):
        return i + 1 if i >= 0 else len(run)

    # Validate that a genuine script, not an ordinary manually-set alarm, is used.
    required(0, 'Load and run script OK')
    if case == 'spring':
        dn1 = required(0, 'Scheduled shutdown is due.', '01:59:00')
        cut1 = required(next_after(dn1), "Cut Raspberry Pi's power.")
        wake = required(next_after(cut1), 'DST: POWMAN timer wake')
        trans = required(next_after(wake), 'DST: enter, RTC adjusted by 3600 seconds.')
        up = required(next_after(trans), 'Scheduled startup is due.', '03:02:00')
        dn2 = required(next_after(up), 'Scheduled shutdown is due.', '03:05:00')
        final_cut = required(next_after(dn2), "Cut Raspberry Pi's power.")
        if up > 0:
            prior_up = [e for e in run[:up] if 'Scheduled startup is due.' in e[1]]
            if prior_up:
                issues.append('Unexpected early startup before 03:02 CEST')
    else:
        dn1 = required(0, 'Scheduled shutdown is due.', '02:54:00')
        cut1 = required(next_after(dn1), "Cut Raspberry Pi's power.")
        up1 = required(next_after(cut1), 'Scheduled startup is due.', '02:56:00')
        dn2 = required(next_after(up1), 'Scheduled shutdown is due.', '02:58:00')
        cut2 = required(next_after(dn2), "Cut Raspberry Pi's power.")
        wake = required(next_after(cut2), 'DST: POWMAN timer wake')
        trans = required(next_after(wake), 'DST: leave, RTC adjusted by -3600 seconds.')
        up2 = required(next_after(trans), 'Scheduled startup is due.', '03:02:00')
        dn3 = required(next_after(up2), 'Scheduled shutdown is due.', '03:05:00')
        final_cut = required(next_after(dn3), "Cut Raspberry Pi's power.")
        if trans >= 0 and up2 >= 0:
            dupes = [e for e in run[trans + 1:up2]
                     if 'Scheduled startup is due.' in e[1]]
            if dupes:
                issues.append('A startup was replayed in the repeated 02:xx hour')
                for e in dupes:
                    print('FAIL: duplicate startup:', e[2])
            else:
                print('PASS: no second startup during the repeated 02:xx CET hour.')

    if final_cut > 0:
        buttons = [e for e in run[:final_cut] if 'Woke up by button.' in e[1] or e[1] == 'Button Down']
        if buttons:
            issues.append('Manual button intervention before the final scheduled power cut')
            for e in buttons:
                print('FAIL: manual button:', e[2])

    if issues:
        print('\nRESULT: FAIL or insufficient evidence')
        print('Review raw log before changing firmware; do not delete evidence.')
        return 1
    print('\nRESULT: PASS (schedule actions + DST + no observed replay)')
    print('Note: This validates the chosen sequence, not all possible scripts or power failures.')
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--case', choices=['spring', 'autumn'], required=True)
    p.add_argument('--log', type=Path, required=True)
    args = p.parse_args()
    if not args.log.is_file():
        p.error(f'Missing log file: {args.log}')
    return analyze(args.log, args.case)


if __name__ == '__main__':
    sys.exit(main())
