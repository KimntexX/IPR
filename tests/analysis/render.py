"""IPR states rendered in memory by iprRenderServer: no IPR TIFF is written.

The server renders through the TIFF export path and sends its 16-bit sRGB
samples on a pipe; the pixels are the ones the TIFF would contain.
"""
import base64
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess

import numpy as np
from PIL import ImageCms

from .native import sha256

ROOT = Path(__file__).resolve().parents[2]
EXECUTABLE = "iprRenderServer.exe" if os.name == "nt" else "iprRenderServer"
# Estimated peak of one server and its client for a 32 MP source: master
# (16 B/px), render (31 B/px), 16-bit buffers on both sides and the cached
# references (about 75 B/px in all). To be confirmed by measurement.
PER_WORKER_BYTES = 5 * 1024 ** 3 // 2
RESERVE_BYTES = 2 * 1024 ** 3


def available_memory():
    """Available physical memory in bytes, None when unknown."""
    if os.name == "nt":
        import ctypes

        class Status(ctypes.Structure):
            _fields_ = [("length", ctypes.c_ulong), ("load", ctypes.c_ulong)] + [
                (name, ctypes.c_ulonglong) for name in ("total", "available", "total_page", "available_page",
                                                        "total_virtual", "available_virtual", "extended")]
        status = Status()
        status.length = ctypes.sizeof(Status)
        return status.available if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)) else None
    try:
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) * 1024
    except OSError:
        pass
    return None


def worker_count(available=None, limit=4):
    """Parallel servers that fit in free memory, keeping a reserve; at least one."""
    available = available_memory() if available is None else available
    if available is None:
        return 1
    by_memory = (available - RESERVE_BYTES) // PER_WORKER_BYTES
    by_cores = max(1, (os.cpu_count() or 2) // 2)
    return int(max(1, min(by_memory, by_cores, limit)))


def find_server():
    """IPR_RENDER_SERVER_BIN, else the Release build of the working tree."""
    explicit = os.environ.get("IPR_RENDER_SERVER_BIN")
    candidates = [Path(explicit)] if explicit else [
        ROOT / "build-windows-msvc/Release" / EXECUTABLE, ROOT / "build" / EXECUTABLE]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise ValueError("iprRenderServer not found; build it or set IPR_RENDER_SERVER_BIN")


def runtime_env(executable):
    """Qt, OpenCV, libtiff and LibRaw runtime folders from the build's CMake cache."""
    env = dict(os.environ)
    cache_path = next((p / "CMakeCache.txt" for p in Path(executable).parents if (p / "CMakeCache.txt").is_file()), None)
    if cache_path is None:
        return env
    cache = {}
    for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line and line[0] not in "#/" and ":" in line and "=" in line:
            key, value = line.split(":", 1)[0], line.split("=", 1)[1]
            cache[key] = value
    folders = []
    if cache.get("Qt6_DIR"):
        qt = Path(cache["Qt6_DIR"]).parents[2]
        folders.append(qt / "bin")
        env.setdefault("QT_PLUGIN_PATH", str(qt / "plugins"))
    if cache.get("OpenCV_DIR"):
        folders.append(Path(cache["OpenCV_DIR"]).parent / "bin")
    for key in ("TIFF_LIBRARY_RELEASE", "LIBRAW_LIBRARY"):
        if cache.get(key):
            library = Path(cache[key]).parent
            if library.name == "manual-link":
                library = library.parent
            folders.append(library.parent / "bin")
    existing = [str(f) for f in dict.fromkeys(folders) if f.is_dir()]
    env["PATH"] = os.pathsep.join(existing + [env.get("PATH", "")])
    return env


class RenderServer:
    """One server process; closed (and killed if needed) on exit."""

    def __init__(self, executable=None, timeout=60):
        self.executable = Path(executable) if executable else find_server()
        self.timeout = timeout
        self.sha256 = sha256(self.executable)
        self.process = subprocess.Popen([str(self.executable)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        env=runtime_env(self.executable), bufsize=0)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        if self.process.poll() is None:
            try:
                self.process.stdin.write(b'{"op":"quit"}\n')
                self.process.stdin.flush()
                self.process.wait(timeout=self.timeout)
            except (OSError, subprocess.TimeoutExpired):
                self.process.kill()
                self.process.wait()
        for stream in (self.process.stdin, self.process.stdout):
            stream.close()

    def _request(self, payload):
        if self.process.poll() is not None:
            raise ValueError("iprRenderServer exited with code " + str(self.process.returncode))
        self.process.stdin.write(json.dumps(payload).encode("utf-8") + b"\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise ValueError("iprRenderServer closed its output")
        header = json.loads(line)
        if not header.get("ok"):
            raise ValueError("iprRenderServer: " + str(header.get("error")))
        return header

    def _read(self, size):
        buffer = bytearray(size)
        view, received = memoryview(buffer), 0
        while received < size:
            count = self.process.stdout.readinto(view[received:])
            if not count:
                raise ValueError("Truncated render from iprRenderServer")
            received += count
        return buffer

    def states(self, base, group):
        return self._request({"op": "list", "base": str(base), "group": group})["states"]

    def render(self, source, feature, value, mixer=None):
        """Pixels (height, width, 3) uint16 and metadata shaped like native.inspect().
        mixer: optional {"hue": [8], "saturation": [8], "luminance": [8]} on top of the state."""
        request = {"op": "render", "source": str(source), "feature": feature, "value": value}
        if mixer is not None:
            request["mixer"] = mixer
        header = self._request(request)
        width, height = header["width"], header["height"]
        if header["channels"] != 3 or header["dtype"] != "<u2" or header["bytes"] != width * height * 6:
            raise ValueError("Unexpected render layout from iprRenderServer")
        data = self._read(header["bytes"])
        pixels = np.frombuffer(data, dtype="<u2").reshape(height, width, 3)
        icc = base64.b64decode(header["icc_base64"])
        if not icc:
            raise ValueError("Render without ICC profile")
        description = ImageCms.getProfileDescription(ImageCms.ImageCmsProfile(io.BytesIO(icc))).strip()
        if "srgb" not in description.lower():
            raise ValueError("Expected sRGB render, got " + description)
        metadata = {"path": None, "render": {"source": str(source), "feature": feature, "value": value},
                    "sha256": hashlib.sha256(data).hexdigest(), "sha256_scope": "uint16 RGB samples, row-major",
                    "bytes": len(data), "dimensions": [width, height], "bits": [16, 16, 16], "channels": 3,
                    "orientation": 1, "compression": 1, "icc": description,
                    "icc_sha256": hashlib.sha256(icc).hexdigest(), "xmp_settings": {},
                    "declaration": header["declaration"]}
        return pixels, metadata

