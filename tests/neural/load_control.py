"""Windows load counters and cooperative execution policy; no training arithmetic."""
import ctypes
from ctypes import wintypes
from datetime import datetime,timezone
import time
from memory_budget import snapshot


def cpu_fractions(previous,current):
    total=current['total']-previous['total'];idle=current['idle']-previous['idle'];own=current['process']-previous['process']
    if total<=0 or min(idle,own)<0:raise ValueError('Monotonic CPU counters required')
    busy=max(0.,min(1.,1-idle/total));process=max(0.,min(1.,own/total))
    return dict(total_cpu_fraction=busy,process_cpu_fraction=process,external_cpu_fraction=max(0.,busy-process))


class LoadMonitor:
    def __init__(self):
        self.api=ctypes.WinDLL('kernel32',use_last_error=True)
        self.api.GetSystemTimes.argtypes=[ctypes.POINTER(wintypes.FILETIME)]*3;self.api.GetSystemTimes.restype=wintypes.BOOL
        self.api.GetCurrentProcess.argtypes=[];self.api.GetCurrentProcess.restype=wintypes.HANDLE
        self.api.GetProcessTimes.argtypes=[wintypes.HANDLE]+[ctypes.POINTER(wintypes.FILETIME)]*4;self.api.GetProcessTimes.restype=wintypes.BOOL
        self.previous=None

    def sample(self):
        fields=[wintypes.FILETIME() for _ in range(7)]
        if not self.api.GetSystemTimes(*[ctypes.byref(v) for v in fields[:3]]):raise ctypes.WinError(ctypes.get_last_error())
        if not self.api.GetProcessTimes(self.api.GetCurrentProcess(),*[ctypes.byref(v) for v in fields[3:]]):raise ctypes.WinError(ctypes.get_last_error())
        values=[v.dwLowDateTime+(v.dwHighDateTime<<32) for v in fields]
        current=dict(idle=values[0],total=values[1]+values[2],process=values[5]+values[6])
        cpu=None if self.previous is None else cpu_fractions(self.previous,current);self.previous=current
        return dict(at_utc=datetime.now(timezone.utc).isoformat(),memory=snapshot(),cpu=cpu)


class YieldController:
    def __init__(self,policy):
        self.policy=policy;self.monitor=LoadMonitor();self.last_tick=0.;self.pressure_since=None;self.last=None

    def check(self):
        now=time.monotonic()
        if now-self.last_tick<2:return False
        self.last_tick=now;self.last=self.monitor.sample()
        cpu=self.last['cpu']
        pressure=(self.last['memory']['system_used_fraction']>=self.policy['yield_memory_fraction']
            or cpu is not None and cpu['external_cpu_fraction']>=self.policy['yield_external_cpu_fraction'])
        if not pressure:self.pressure_since=None;return False
        if self.pressure_since is None:self.pressure_since=now
        return now-self.pressure_since>=self.policy['pressure_seconds']
