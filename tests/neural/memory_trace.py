"""Bounded, phase-labelled memory telemetry; no CUDA calls from the observer thread."""
from collections import deque
from datetime import datetime,timezone
import json
from pathlib import Path
import threading
import time


class MemoryTrace:
    def __init__(self,path,maximum_bytes=16*1024**2,ring_size=64):
        if maximum_bytes<1024 or ring_size<1:raise ValueError('Positive bounded trace required')
        self.path=Path(path);self.maximum_bytes=maximum_bytes
        self.lock=threading.Lock();self.stream=None;self.started=time.monotonic()
        self.context=dict(phase='initialization',step=0,micro=None,gpu=None)
        self.ring=deque(maxlen=ring_size);self.first_crossing=None
        self.written=0;self.samples=0;self.truncated=False

    def phase(self,name,step=0,micro=None,gpu=None):
        if not isinstance(name,str) or not name or step<0:raise ValueError('Named phase and nonnegative step required')
        with self.lock:self.context=dict(phase=name,step=step,micro=micro,gpu=gpu)

    def observe(self,sample,reason):
        record=dict(at_utc=datetime.now(timezone.utc).isoformat(),elapsed_seconds=time.monotonic()-self.started,
            counters=dict(sample),reason=reason)
        with self.lock:
            if self.stream is None:raise RuntimeError('Trace observer outside open lifetime')
            record.update(self.context);self.samples+=1;self.ring.append(record)
            if reason and self.first_crossing is None:
                self.first_crossing=dict(event=record,preceding_and_crossing=list(self.ring))
            encoded=(json.dumps(record,allow_nan=False)+'\n').encode('utf-8')
            if self.written+len(encoded)<=self.maximum_bytes:
                self.stream.write(encoded);self.stream.flush();self.written+=len(encoded)
            else:self.truncated=True

    def stats(self):
        with self.lock:
            return dict(path=str(self.path.resolve()),maximum_bytes=self.maximum_bytes,written_bytes=self.written,
                samples=self.samples,truncated=self.truncated,first_crossing=self.first_crossing,
                last_samples=list(self.ring),gpu_scope='Main-thread CUDA allocation/reservation counters at last phase transition; not GPU driver/shared-system attribution.')

    def __enter__(self):
        if self.stream is not None:raise RuntimeError('Trace already opened')
        self.stream=self.path.open('xb');return self

    def __exit__(self,*_):
        with self.lock:
            if self.stream is not None:self.stream.close();self.stream=None
