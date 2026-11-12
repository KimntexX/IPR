"""Explicit Windows sleep with privilege lifetime and read-only process/input checks."""
from contextlib import contextmanager
import ctypes
from ctypes import wintypes


class Luid(ctypes.Structure):
    _fields_=[('low',wintypes.DWORD),('high',wintypes.LONG)]


class Privilege(ctypes.Structure):
    _fields_=[('luid',Luid),('attributes',wintypes.DWORD)]


class TokenPrivileges(ctypes.Structure):
    _fields_=[('count',wintypes.DWORD),('entries',Privilege*1)]


class LastInput(ctypes.Structure):
    _fields_=[('size',wintypes.UINT),('tick',wintypes.DWORD)]


@contextmanager
def shutdown_privilege():
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);security=ctypes.WinDLL('advapi32',use_last_error=True)
    kernel.GetCurrentProcess.argtypes=[];kernel.GetCurrentProcess.restype=wintypes.HANDLE
    kernel.CloseHandle.argtypes=[wintypes.HANDLE];kernel.CloseHandle.restype=wintypes.BOOL
    security.OpenProcessToken.argtypes=[wintypes.HANDLE,wintypes.DWORD,ctypes.POINTER(wintypes.HANDLE)];security.OpenProcessToken.restype=wintypes.BOOL
    security.LookupPrivilegeValueW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR,ctypes.POINTER(Luid)];security.LookupPrivilegeValueW.restype=wintypes.BOOL
    security.AdjustTokenPrivileges.argtypes=[wintypes.HANDLE,wintypes.BOOL,ctypes.POINTER(TokenPrivileges),wintypes.DWORD,ctypes.POINTER(TokenPrivileges),ctypes.POINTER(wintypes.DWORD)]
    security.AdjustTokenPrivileges.restype=wintypes.BOOL
    token=wintypes.HANDLE();old=TokenPrivileges();changed=False
    if not security.OpenProcessToken(kernel.GetCurrentProcess(),0x20|0x8,ctypes.byref(token)):raise ctypes.WinError(ctypes.get_last_error())
    try:
        wanted=TokenPrivileges();wanted.count=1;wanted.entries[0].attributes=2
        if not security.LookupPrivilegeValueW(None,'SeShutdownPrivilege',ctypes.byref(wanted.entries[0].luid)):raise ctypes.WinError(ctypes.get_last_error())
        returned=wintypes.DWORD();ctypes.set_last_error(0)
        if not security.AdjustTokenPrivileges(token,False,ctypes.byref(wanted),ctypes.sizeof(old),ctypes.byref(old),ctypes.byref(returned)):
            raise ctypes.WinError(ctypes.get_last_error())
        error=ctypes.get_last_error()
        if error:raise ctypes.WinError(error)
        changed=True;yield
    finally:
        if changed and old.count:security.AdjustTokenPrivileges(token,False,ctypes.byref(old),0,None,None)
        kernel.CloseHandle(token)


def probe_sleep_permission():
    with shutdown_privilege():return True


def suspend():
    power=ctypes.WinDLL('powrprof',use_last_error=True)
    power.SetSuspendState.argtypes=[ctypes.c_ubyte]*3;power.SetSuspendState.restype=ctypes.c_ubyte
    with shutdown_privilege():
        if not power.SetSuspendState(False,False,False):raise ctypes.WinError(ctypes.get_last_error())


def input_idle_seconds():
    user=ctypes.WinDLL('user32',use_last_error=True);kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    user.GetLastInputInfo.argtypes=[ctypes.POINTER(LastInput)];user.GetLastInputInfo.restype=wintypes.BOOL
    kernel.GetTickCount64.argtypes=[];kernel.GetTickCount64.restype=ctypes.c_ulonglong
    value=LastInput();value.size=ctypes.sizeof(value)
    if not user.GetLastInputInfo(ctypes.byref(value)):raise ctypes.WinError(ctypes.get_last_error())
    return ((kernel.GetTickCount64()-value.tick)&0xffffffff)/1000.


def process_identity(pid):
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.GetProcessTimes.argtypes=[wintypes.HANDLE]+[ctypes.POINTER(wintypes.FILETIME)]*4;kernel.GetProcessTimes.restype=wintypes.BOOL
    kernel.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD];kernel.WaitForSingleObject.restype=wintypes.DWORD
    kernel.CloseHandle.argtypes=[wintypes.HANDLE];kernel.CloseHandle.restype=wintypes.BOOL
    handle=kernel.OpenProcess(0x1000|0x100000,False,pid)
    if not handle:return None
    try:
        if kernel.WaitForSingleObject(handle,0)!=258:return None
        fields=[wintypes.FILETIME() for _ in range(4)]
        if not kernel.GetProcessTimes(handle,*[ctypes.byref(v) for v in fields]):raise ctypes.WinError(ctypes.get_last_error())
        return fields[0].dwLowDateTime+(fields[0].dwHighDateTime<<32)
    finally:kernel.CloseHandle(handle)
