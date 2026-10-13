#!/usr/bin/env python3
"""Classify model checks separately from native startup/shutdown evidence."""
import argparse
import json
from pathlib import Path
import re


def classify(log, exit_status):
    passes = re.findall(r'^ASSISTANT_SELF_TEST_PASS .+$', log, re.MULTILINE)
    failures = []
    if exit_status == 124:
        failures.append('40-second process timeout; native lifecycle did not complete')
    elif exit_status != 0:
        failures.append(f'process/wrapper exited with status {exit_status}')
    if len(passes) != 1:
        failures.append(f'expected one model-check PASS marker, found {len(passes)}')
    if re.search(r'^ASSISTANT_SELF_TEST_FAIL\b', log, re.MULTILINE):
        failures.append('internal self-test failure')
    if re.search(r'assert(?:ion)?.*failed|XARA_ASSERT_BACKTRACE_BEGIN', log, re.IGNORECASE):
        failures.append('startup/shutdown assertion detected')
    if re.search(r'(?:Gtk|GLib(?:-GObject)?)-CRITICAL|Segmentation fault|Aborted|SIGSEGV', log, re.IGNORECASE):
        failures.append('critical GTK/GLib error or crash detected')
    warnings = [line for line in log.splitlines() if 'Gtk-WARNING' in line or 'negative dimensions' in line]
    return {
        'application_wrapper_exit': exit_status,
        'model_checks_passed': len(passes) == 1 and not re.search(r'^ASSISTANT_SELF_TEST_FAIL\b', log, re.MULTILINE),
        'lifecycle_completed': exit_status == 0,
        'smoke_passed': not failures,
        'failure_reasons': failures,
        'gtk_warnings': warnings,
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument('log', type=Path)
    p.add_argument('exit_status', type=int)
    p.add_argument('--status', type=Path, required=True)
    a = p.parse_args()
    result = classify(a.log.read_text(errors='replace'), a.exit_status)
    a.status.write_text(json.dumps(result, indent=2) + '\n')
    if result['smoke_passed']:
        print('PASS: native model checks and process exit 0; manual panel checks remain separate.')
        if result['gtk_warnings']:
            print('GTK warnings retained in startup.log/status.json; this is not a warning-free run.')
        return 0
    for reason in result['failure_reasons']:
        print('FAIL: ' + reason)
    if result['model_checks_passed']:
        print('Model checks passed; the overall smoke test failed for the reasons above.')
    return 1


if __name__ == '__main__':
    raise SystemExit(main())
