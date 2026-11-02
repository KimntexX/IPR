"""Windows bench-only memory quota and cooperative physical-memory admission guard.

The OS quota limits committed process memory, not total machine RSS. The
physical/system thresholds are sampled and cooperative, not global OS caps.
"""
import ctypes
from ctypes import wintypes
import os
import threading


class BudgetExceeded(RuntimeError):
    pass


class MemoryStatus(ctypes.Structure):
    _fields_ = [('length', wintypes.DWORD), ('load', wintypes.DWORD)]+[
        (n, ctypes.c_ulonglong) for n in ('total', 'available', 'total_pagefile', 'available_pagefile',
            'total_virtual', 'available_virtual', 'extended_virtual')]


class ProcessCounters(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('faults', wintypes.DWORD)]+[
        (n, ctypes.c_size_t) for n in ('peak_rss', 'rss', 'peak_paged_pool', 'paged_pool',
            'peak_nonpaged_pool', 'nonpaged_pool', 'pagefile', 'peak_pagefile', 'private')]


class BasicLimits(ctypes.Structure):
    _fields_ = [('process_time', ctypes.c_longlong), ('job_time', ctypes.c_longlong),
        ('flags', wintypes.DWORD), ('minimum_working_set', ctypes.c_size_t),
        ('maximum_working_set', ctypes.c_size_t), ('active_processes', wintypes.DWORD),
        ('affinity', ctypes.c_size_t), ('priority', wintypes.DWORD), ('scheduling', wintypes.DWORD)]


class IoCounters(ctypes.Structure):
    _fields_ = [(n, ctypes.c_ulonglong) for n in ('read_ops', 'write_ops', 'other_ops', 'read_bytes', 'write_bytes', 'other_bytes')]


class ExtendedLimits(ctypes.Structure):
    _fields_ = [('basic', BasicLimits), ('io', IoCounters), ('process_limit', ctypes.c_size_t),
        ('job_limit', ctypes.c_size_t), ('peak_process', ctypes.c_size_t), ('peak_job', ctypes.c_size_t)]


def api():
    if os.name != 'nt':
        raise RuntimeError('This hard-quota bench currently requires Windows; refusing an unguarded fallback')
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    signatures = dict(GetCurrentProcess=([], wintypes.HANDLE),
        GlobalMemoryStatusEx=([ctypes.POINTER(MemoryStatus)], wintypes.BOOL),
        K32GetProcessMemoryInfo=([wintypes.HANDLE, ctypes.POINTER(ProcessCounters), wintypes.DWORD], wintypes.BOOL),
        CreateJobObjectW=([ctypes.c_void_p, wintypes.LPCWSTR], wintypes.HANDLE),
        SetInformationJobObject=([wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD], wintypes.BOOL),
        QueryInformationJobObject=([wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p], wintypes.BOOL),
        AssignProcessToJobObject=([wintypes.HANDLE, wintypes.HANDLE], wintypes.BOOL),
        CloseHandle=([wintypes.HANDLE], wintypes.BOOL))
    for name, (args, result) in signatures.items():
        getattr(k, name).argtypes = args
        getattr(k, name).restype = result
    return k


def snapshot(k=None):
    k = k or api()
    system = MemoryStatus()
    system.length = ctypes.sizeof(system)
    process = ProcessCounters()
    process.cb = ctypes.sizeof(process)
    if not k.GlobalMemoryStatusEx(ctypes.byref(system)) or not k.K32GetProcessMemoryInfo(k.GetCurrentProcess(), ctypes.byref(process), process.cb):
        raise ctypes.WinError(ctypes.get_last_error())
    return dict(system_total_bytes=system.total, system_available_bytes=system.available,
        system_used_fraction=1-system.available/system.total, process_rss_bytes=process.rss,
        process_private_bytes=process.private, process_peak_rss_bytes=process.peak_rss,
        process_peak_pagefile_bytes=process.peak_pagefile)


def violation(sample, soft_bytes, system_fraction):
    if sample['process_private_bytes'] >= soft_bytes or sample['process_rss_bytes'] >= soft_bytes:
        return 'process_soft_memory_limit'
    if sample['system_used_fraction'] >= system_fraction:
        return 'system_physical_memory_threshold'
    return None


class MemoryGuard:
    def __init__(self, hard_bytes=2*1024**3, soft_bytes=1280*1024**2, system_fraction=.8, interval=.25):
        if not 0 < soft_bytes < hard_bytes or not 0 < system_fraction < 1 or interval <= 0:
            raise ValueError('Positive ordered memory budgets and valid monitoring policy required')
        self.hard_bytes, self.soft_bytes = int(hard_bytes), int(soft_bytes)
        self.system_fraction, self.interval = system_fraction, interval
        self.k, self.job, self.thread = None, None, None
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.reason = None
        self.observations = 0
        self.max_rss = self.max_private = self.max_system = 0
        self.job_peak = 0
        self.last = None
        self.entered = False

    def _observe(self):
        sample = snapshot(self.k)
        with self.lock:
            self.last = sample
            self.observations += 1
            self.max_rss = max(self.max_rss, sample['process_rss_bytes'], sample['process_peak_rss_bytes'])
            self.max_private = max(self.max_private, sample['process_private_bytes'])
            self.max_system = max(self.max_system, sample['system_used_fraction'])
            self.reason = self.reason or violation(sample, self.soft_bytes, self.system_fraction)

    def _monitor(self):
        while not self.stop.wait(self.interval):
            try:
                self._observe()
            except Exception as error:
                with self.lock:
                    self.reason = self.reason or 'monitor_failure: '+str(error)
                return

    def __enter__(self):
        if self.entered:
            raise RuntimeError('Memory guard instances are single-use')
        self.entered = True
        self.k = api()
        self._observe()
        self.check()
        self.job = self.k.CreateJobObjectW(None, None)
        if not self.job:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            limits = ExtendedLimits()
            limits.basic.flags = 0x100  # JOB_OBJECT_LIMIT_PROCESS_MEMORY
            limits.process_limit = self.hard_bytes
            if (not self.k.SetInformationJobObject(self.job, 9, ctypes.byref(limits), ctypes.sizeof(limits))
                    or not self.k.AssignProcessToJobObject(self.job, self.k.GetCurrentProcess())):
                raise ctypes.WinError(ctypes.get_last_error())
            self.thread = threading.Thread(target=self._monitor, name='ipr-bench-memory', daemon=True)
            self.thread.start()
        except Exception:
            self.close()
            raise
        return self

    def check(self):
        with self.lock:
            reason = self.reason
        if reason:
            raise BudgetExceeded(reason)

    def stats(self):
        with self.lock:
            return dict(hard_process_commit_bytes=self.hard_bytes, soft_process_bytes=self.soft_bytes,
                system_stop_fraction=self.system_fraction, poll_seconds=self.interval, samples=self.observations,
                peak_rss_bytes=self.max_rss, peak_private_sampled_bytes=self.max_private,
                peak_system_used_fraction=self.max_system, os_job_peak_commit_bytes=self.job_peak,
                reason=self.reason, last=self.last)

    def close(self):
        self.stop.set()
        alive = False
        try:
            if self.thread is not None:
                self.thread.join(timeout=2.)
                alive = self.thread.is_alive()
        finally:
            if self.job:
                limits = ExtendedLimits()
                if self.k.QueryInformationJobObject(self.job, 9, ctypes.byref(limits), ctypes.sizeof(limits), None):
                    self.job_peak = limits.peak_process
                self.k.CloseHandle(self.job)
                self.job = None
        if alive:
            raise RuntimeError('Memory monitor failed to terminate within2seconds')

    def __exit__(self, *_):
        self.close()


if __name__ == '__main__':
    import json
    print(json.dumps(snapshot(), indent=2))
