"""Detached local command runner with persistent status/logs and finite timeout."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys


def windows_group():
    """Own a kill-on-close Job Object, so a timeout cannot orphan native children."""
    import ctypes as c
    from ctypes import wintypes as w
    class Basic(c.Structure):
        _fields_=[("process_time",c.c_longlong),("job_time",c.c_longlong),("flags",w.DWORD),
                  ("min_ws",c.c_size_t),("max_ws",c.c_size_t),("active",w.DWORD),("affinity",c.c_size_t),
                  ("priority",w.DWORD),("scheduling",w.DWORD)]
    class IO(c.Structure):
        _fields_=[(name,c.c_ulonglong) for name in ("reads","writes","other","read_bytes","write_bytes","other_bytes")]
    class Limits(c.Structure):
        _fields_=[("basic",Basic),("io",IO),("process_memory",c.c_size_t),("job_memory",c.c_size_t),
                  ("peak_process",c.c_size_t),("peak_job",c.c_size_t)]
    kernel=c.WinDLL("kernel32",use_last_error=True)
    kernel.CreateJobObjectW.argtypes=[c.c_void_p,w.LPCWSTR];kernel.CreateJobObjectW.restype=w.HANDLE
    kernel.SetInformationJobObject.argtypes=[w.HANDLE,c.c_int,c.c_void_p,w.DWORD];kernel.SetInformationJobObject.restype=w.BOOL
    kernel.AssignProcessToJobObject.argtypes=[w.HANDLE,w.HANDLE];kernel.AssignProcessToJobObject.restype=w.BOOL
    kernel.CloseHandle.argtypes=[w.HANDLE];kernel.CloseHandle.restype=w.BOOL
    handle=kernel.CreateJobObjectW(None,None)
    if not handle:raise c.WinError(c.get_last_error())
    limits=Limits();limits.basic.flags=0x2000 # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not kernel.SetInformationJobObject(handle,9,c.byref(limits),c.sizeof(limits)):
        error=c.get_last_error();kernel.CloseHandle(handle);raise c.WinError(error)
    return kernel,handle


def save(path, record):
    temporary=path.with_suffix(".tmp")
    temporary.write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding="utf-8")
    temporary.replace(path)


def worker(spec_path):
    spec_path=Path(spec_path);spec=json.loads(spec_path.read_text(encoding="utf-8"));folder=spec_path.parent
    state=dict(name=spec["name"],pid=os.getpid(),status="running",started_at_utc=datetime.now(timezone.utc).isoformat())
    save(folder/"status.json",state)
    try:
        with (folder/"stdout.log").open("w",encoding="utf-8") as out,(folder/"stderr.log").open("w",encoding="utf-8") as err:
            group=windows_group() if sys.platform=="win32" else None
            process=None
            try:
                process=subprocess.Popen(spec["argv"],cwd=spec["cwd"],stdout=out,stderr=err,stdin=subprocess.DEVNULL,
                                         start_new_session=sys.platform!="win32")
                if group:
                    import ctypes
                    if not group[0].AssignProcessToJobObject(group[1],int(process._handle)):
                        raise ctypes.WinError(ctypes.get_last_error())
                code=process.wait(timeout=spec["timeout_seconds"])
            finally:
                if group:group[0].CloseHandle(group[1])
                if process is not None and process.poll() is None:
                    if sys.platform=="win32":process.kill()
                    else:os.killpg(process.pid,signal.SIGKILL)
                    process.wait(timeout=10)
        state.update(status="succeeded" if code==0 else "failed",exit_code=code)
    except Exception as exc:
        state.update(status="failed",exit_code=None,error=str(exc))
    state["finished_at_utc"]=datetime.now(timezone.utc).isoformat();save(folder/"status.json",state)
    if sys.platform=="win32":
        try:
            import winsound
            winsound.MessageBeep(winsound.MB_OK if state["status"]=="succeeded" else winsound.MB_ICONHAND)
            state["notification"]="Windows sound requested; may be disabled in system settings"
        except Exception as exc:state["notification"]=f"sound unavailable: {exc}"
        save(folder/"status.json",state)
    return 0 if state["status"]=="succeeded" else 1


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest="operation",required=True)
    start=sub.add_parser("start");start.add_argument("--name",required=True)
    start.add_argument("--cwd",type=Path,default=Path.cwd());start.add_argument("--timeout",type=int,default=2100)
    start.add_argument("--job-root",type=Path,default=Path("build-patch_noise/jobs"))
    start.add_argument("command",nargs=argparse.REMAINDER)
    run=sub.add_parser("worker");run.add_argument("spec",type=Path)
    args=parser.parse_args()
    if args.operation=="worker":return worker(args.spec)
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,80}",args.name) or args.timeout<=0:raise ValueError("invalid job name/timeout")
    command=args.command[1:] if args.command[:1]==["--"] else args.command
    if not command:raise ValueError("missing command")
    cwd=args.cwd.resolve(strict=True)
    if not cwd.is_dir():raise ValueError("job cwd is not a folder")
    folder=(args.job_root/args.name).resolve();folder.mkdir(parents=True,exist_ok=False)
    save(folder/"spec.json",dict(name=args.name,cwd=str(cwd),argv=command,timeout_seconds=args.timeout))
    save(folder/"status.json",dict(name=args.name,status="queued",queued_at_utc=datetime.now(timezone.utc).isoformat()))
    kwargs={"creationflags":subprocess.DETACHED_PROCESS|subprocess.CREATE_NEW_PROCESS_GROUP} if sys.platform=="win32" else {"start_new_session":True}
    process=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),"worker",str(folder/"spec.json")],
                             stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,close_fds=True,**kwargs)
    print(json.dumps(dict(pid=process.pid,job_directory=str(folder)),ensure_ascii=False))
    return 0


if __name__=="__main__":raise SystemExit(main())
