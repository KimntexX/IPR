"""Run one owned native bench with process, physical RAM and timeout guards."""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import subprocess
import time

from memory_budget import MemoryGuard,ProcessCounters,BudgetExceeded


def run(output,command,hard_mib=2048,soft_mib=1280):
    root=Path(__file__).resolve().parents[2]
    if not output.resolve().is_relative_to((root/'build-patch_neural_restoration').resolve()) or not command:
        raise ValueError('Dedicated output and one command required')
    if not 0<soft_mib<=hard_mib<=6144:
        raise ValueError('Positive bounded native-memory budgets required')
    output.mkdir(parents=True,exist_ok=False)
    command=[str(Path(command[0]).resolve()),*command[1:]]
    report=dict(status='started',command=command,started_at_utc=datetime.now(timezone.utc).isoformat(),
        child_peak_private_bytes=0,child_peak_rss_bytes=0,timeout_seconds=900)
    guard=MemoryGuard(hard_bytes=hard_mib*1024**2,soft_bytes=soft_mib*1024**2,system_fraction=.92)
    started=time.monotonic()
    child=None
    try:
        with guard,(output/'process.log').open('x',encoding='utf-8') as log:
            guard.k.SetErrorMode(0x0001|0x0002)
            env=dict(os.environ)
            env['PATH']='C:/Qt/6.11.2/msvc2022_64/bin;C:/dev/vcpkg/installed/x64-windows/bin;C:/opencv/build/x64/vc16/bin;'+env.get('PATH','')
            child=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,env=env)
            while child.poll() is None:
                guard.check()
                counters=ProcessCounters()
                counters.cb=ctypes.sizeof(counters)
                if not guard.k.K32GetProcessMemoryInfo(wintypes.HANDLE(int(child._handle)),ctypes.byref(counters),counters.cb):
                    if child.poll() is None:
                        raise ctypes.WinError(ctypes.get_last_error())
                else:
                    report['child_peak_private_bytes']=max(report['child_peak_private_bytes'],int(counters.private))
                    report['child_peak_rss_bytes']=max(report['child_peak_rss_bytes'],int(counters.rss))
                    if counters.private>soft_mib*1024**2 or counters.rss>soft_mib*1024**2:
                        raise BudgetExceeded('Native child soft memory limit')
                if time.monotonic()-started>report['timeout_seconds']:
                    raise TimeoutError('Native bench timed out')
                time.sleep(.25)
            report['returncode']=child.wait(timeout=10)
            if report['returncode']:
                raise RuntimeError('Native bench failed; see process.log')
            report['status']='completed_native_bench'
    except Exception as error:
        report.update(status='stopped_on_error',error=str(error))
        raise
    finally:
        if child is not None and child.poll() is None:
            subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],capture_output=True,timeout=10,check=False)
            child.wait(timeout=10)
        report['elapsed_seconds']=time.monotonic()-started
        report['memory']=guard.stats()
        (output/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--hard-mib',type=int,default=2048)
    parser.add_argument('--soft-mib',type=int,default=1280)
    parser.add_argument('command',nargs=argparse.REMAINDER)
    args=parser.parse_args()
    run(args.output,args.command[1:] if args.command[:1]==['--'] else args.command,args.hard_mib,args.soft_mib)
