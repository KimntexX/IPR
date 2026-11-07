"""User-authorized sleep after both queued campaigns close, never after cutoff."""
import argparse
import ctypes
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import time

from prepare_next_training import read, write
from windows_sleep import input_idle_seconds, process_identity, probe_sleep_permission, suspend


def finished(state, active):
    """An archive timestamp alone cannot prove that the worker has exited."""
    pids = [state.get('pid')] + [r.get('pid') for r in state.get('stages', [])]
    return bool(state.get('finished_at_utc')) and not any(active(pid) for pid in pids if pid)


def blockers(processes, own_pid):
    found = []
    for row in processes:
        command = row.get('CommandLine') or ''
        if row['ProcessId'] == own_pid or 'queued_sleep_watch.py' in command:
            continue
        if 'run-jedi-language-server.py' in command or 'jedilsp' in command:
            continue
        found.append(row)
    return found


def compute_processes():
    command = "$ErrorActionPreference='Stop'; @(Get-CimInstance Win32_Process | Where-Object { $_.Name -match '^(pythonw?\\.exe|appIPR\\.exe|iprRenderServer\\.exe|iprGenerateTests\\.exe|cmake\\.exe|ninja\\.exe|cl\\.exe|MSBuild\\.exe)$' } | Select-Object ProcessId,Name,CommandLine) | ConvertTo-Json -Compress"
    output = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', command],
        capture_output=True, text=True, timeout=30, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    rows = json.loads(output.stdout)
    return rows if isinstance(rows, list) else [rows]


def run(output, cutoff):
    root = Path(__file__).resolve().parents[2]
    build = root/'build-patch_neural_restoration'
    progress = [build/'dru-composite-study-v001/progress.json', build/'training-preparation-v001/progress.json']
    deadline = datetime.fromisoformat(cutoff)
    if deadline.utcoffset() is None:
        raise ValueError('Explicit timezone required')
    output.mkdir(exist_ok=False)
    state = dict(status='watching_both_campaigns', pid=os.getpid(), cutoff_utc=cutoff,
                 started_at_utc=datetime.now(timezone.utc).isoformat(), campaigns=list(map(str, progress)))
    power = ctypes.WinDLL('kernel32', use_last_error=True)
    power.SetThreadExecutionState.argtypes = [ctypes.c_uint]
    power.SetThreadExecutionState.restype = ctypes.c_uint
    try:
        probe_sleep_permission()
        state['permission_probe_passed'] = True
        if not power.SetThreadExecutionState(0x80000001):
            raise ctypes.WinError(ctypes.get_last_error())
        while datetime.now(timezone.utc) < deadline:
            if (output/'CANCEL_SLEEP').exists():
                state['status'] = 'cancelled'
                return
            states = [read(path) for path in progress]
            ready = all(finished(item, lambda pid: process_identity(pid) is not None) for item in states)
            state['campaign_states'] = [s.get('status') for s in states]
            state['last_check_utc'] = datetime.now(timezone.utc).isoformat()
            state['status'] = 'waiting_for_campaigns'
            if ready:
                state['blocking_processes'] = blockers(compute_processes(), os.getpid())
                state['idle_seconds'] = input_idle_seconds()
                state['status'] = 'waiting_for_idle_or_other_work'
                if not state['blocking_processes'] and state['idle_seconds'] >= 180:
                    if datetime.now(timezone.utc) >= deadline or (output/'CANCEL_SLEEP').exists():
                        continue
                    state['status'] = 'sleep_requested'
                    write(output/'progress.json', state)
                    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
                    with (root/'docs/validation/raw'/f'neural-queued-sleep-v001-{stamp}.json').open('x', encoding='utf-8') as stream:
                        json.dump(state, stream, indent=2, allow_nan=False)
                    power.SetThreadExecutionState(0x80000000)
                    suspend()
                    state['status'] = 'returned_from_sleep'
                    return
            write(output/'progress.json', state)
            time.sleep(30)
        state['status'] = 'cutoff_reached_no_late_sleep'
    except Exception as error:
        state.update(status='watch_failed_no_sleep_assumed', error=str(error))
    finally:
        power.SetThreadExecutionState(0x80000000)
        state['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
        write(output/'progress.json', state)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cutoff', required=True)
    args = parser.parse_args()
    run(args.output, args.cutoff)
