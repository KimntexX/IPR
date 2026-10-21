"""Strict native TIFF reader. Metadata via Pillow, pixels via read-only memmap."""
import hashlib
import io
import json
import os
from pathlib import Path
import re
from datetime import datetime, timezone
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image, ImageCms

CRS = "http://ns.adobe.com/camera-raw-settings/1.0/"


def read_json(path):
    def reject(value):
        raise ValueError("Non-finite JSON number: " + value)
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate JSON key: " + key)
            result[key] = value
        return result
    return json.loads(Path(path).read_text(encoding="utf-8-sig"),
                      parse_constant=reject, object_pairs_hook=unique)


def local_path(value, root=None):
    value = str(value).replace("\\", "/")
    if os.name != "nt" and re.match(r"^[A-Za-z]:/", value):
        value = "/mnt/" + value[0].lower() + value[2:]
    path = Path(value)
    return (Path(root) / path if root is not None and not path.is_absolute() else path).resolve()


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(4 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def signature(path):
    stat = Path(path).stat()
    return stat.st_size, stat.st_mtime_ns


def linearize(encoded):
    return np.where(encoded <= .04045, encoded / 12.92, ((encoded + .055) / 1.055) ** 2.4)


def embedded_metadata(path):
    """Read declarations without allocating pixels or pretending to audit them."""
    with Image.open(path) as image:
        if image.format != "TIFF":
            raise ValueError("Expected TIFF: " + str(path))
        description = image.tag_v2.get(270, "")
        if isinstance(description, bytes):
            description = description.decode("utf-8")
        ipr = None
        if description and str(description).lstrip().startswith("{"):
            ipr = json.loads(description)
            if not isinstance(ipr, dict) or ipr.get("ipr_test_schema") != 1:
                ipr = None
        xmp = image.tag_v2.get(700, b"")
        if isinstance(xmp, tuple):
            xmp = bytes(xmp)
        settings = {}
        if xmp:
            try:
                elements = list(ET.fromstring(xmp).iter())
            except ET.ParseError as error:
                raise ValueError("Invalid embedded XMP: " + str(path)) from error
            for element in elements:
                for key, value in element.attrib.items():
                    if key.startswith("{" + CRS + "}"):
                        settings[key.split("}")[1]] = value
                if element.tag.startswith("{" + CRS + "}") and element.text:
                    settings[element.tag.split("}")[1]] = element.text.strip()
        return ipr, settings


def inspect(path):
    path = Path(path)
    before = signature(path)
    digest = sha256(path)
    with Image.open(path) as image:
        if image.format != "TIFF" or getattr(image, "n_frames", 1) != 1:
            raise ValueError("Expected single-page TIFF: " + str(path))
        tags = image.tag_v2
        width, height = image.size
        channels = int(tags.get(277, 3))
        bits = tuple(tags.get(258, ()))
        sample_format = tags.get(339, (1,) * channels)
        if (channels not in (3, 4) or bits != (16,) * channels or
                tags.get(262) != 2 or tags.get(259, 1) != 1 or
                tags.get(284, 1) != 1 or tags.get(274, 1) != 1 or
                sample_format not in (1, (1,) * channels)):
            raise ValueError("Expected uncompressed contiguous RGB(A) uint16 orientation 1: " + str(path))
        icc = image.info.get("icc_profile", b"")
        if not icc:
            raise ValueError("Missing ICC: " + str(path))
        profile = ImageCms.ImageCmsProfile(io.BytesIO(icc))
        description = ImageCms.getProfileDescription(profile).strip()
        # An arbitrary ICC is not converted silently or assumed sRGB.
        if "srgb" not in description.lower():
            raise ValueError("Expected embedded sRGB ICC (no implicit conversion): " + str(path))
        offsets, counts = tuple(tags.get(273, ())), tuple(tags.get(279, ()))
        expected = width * height * channels * 2
        if (not offsets or len(offsets) != len(counts) or sum(counts) != expected or
                any(offsets[i + 1] != offsets[i] + counts[i] for i in range(len(offsets) - 1)) or
                offsets[0] + expected > before[0]):
            raise ValueError("Incomplete/noncontiguous TIFF strips: " + str(path))
        settings = {}
        xmp = tags.get(700, b"")
        if isinstance(xmp, tuple):
            xmp = bytes(xmp)
        if xmp:
            try:
                elements = list(ET.fromstring(xmp).iter())
            except ET.ParseError as error:
                raise ValueError("Invalid embedded XMP: " + str(path)) from error
            for element in elements:
                for key, value in element.attrib.items():
                    if key.startswith("{" + CRS + "}"):
                        settings[key.split("}")[1]] = value
                if element.tag.startswith("{" + CRS + "}") and element.text:
                    settings[element.tag.split("}")[1]] = element.text.strip()
    with path.open("rb") as stream:
        order = stream.read(2)
    if order not in (b"II", b"MM"):
        raise ValueError("Invalid TIFF byte order")
    pixels = np.memmap(path, mode="r", dtype="<u2" if order == b"II" else ">u2",
                       offset=offsets[0], shape=(height, width, channels))
    if channels == 4:
        # Check all alpha pixels, not only the analysis sample.
        for row in range(0, height, 256):
            if np.any(pixels[row:row + 256, :, 3] != 65535):
                raise ValueError("Non-opaque alpha is outside this protocol")
    if signature(path) != before:
        raise ValueError("File changed during inspection: " + str(path))
    metadata = {"path": str(path), "sha256": digest, "bytes": before[0],
                "modified_at_utc": datetime.fromtimestamp(path.stat().st_mtime, timezone.utc).isoformat(),
                "dimensions": [width, height], "bits": list(bits), "channels": channels,
                "orientation": 1, "compression": 1, "icc": description,
                "icc_sha256": hashlib.sha256(icc).hexdigest(), "xmp_settings": settings}
    return pixels, metadata, before


def encoded(pixels, box=None, step=1):
    if box is None:
        return np.asarray(pixels[::step, ::step, :3], dtype=np.float64) / 65535.
    top, left, height, width = box
    return np.asarray(pixels[top:top + height:step, left:left + width:step, :3], dtype=np.float64) / 65535.


def verify_unchanged(path, before, digest):
    if signature(path) != before or sha256(path) != digest:
        raise ValueError("Input changed during campaign: " + str(path))


def write_immutable(path, result):
    """Exclusive publication; strict JSON; remove only our incomplete output on error."""
    path = Path(path)
    payload = json.dumps(result, ensure_ascii=False, allow_nan=False, indent=2) + "\n"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as output:
        try:
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        except BaseException:
            output.close()
            path.unlink()
            raise
