import unittest

import numpy as np

from preview_camera_neural import display_window, gray_view, write_png, read_camera_rgb


class CameraNeuralPreviewTests(unittest.TestCase):
    def test_camera_tile_offset_and_truncation(self):
        from pathlib import Path
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            data = np.arange(384*384*3, dtype=np.float32).reshape(384, 384, 3)
            path = folder/'pixels.bin'
            path.write_bytes(b'header!!'+data.tobytes())
            camera = dict(descriptor=str(folder/'camera.json'), pixels=path.name)
            tile = dict(dtype='<f4', shape=[384, 384, 3], offset_bytes=8)
            np.testing.assert_array_equal(read_camera_rgb(camera, tile), data)
            path.write_bytes(b'header!!'+data.tobytes()[:-4])
            with self.assertRaises(ValueError):
                read_camera_rgb(camera, tile)
            with self.assertRaises(ValueError):
                read_camera_rgb(camera, dict(tile, offset_bytes=-1))

    def test_png_chunks_decode_and_source_file_remains_immutable(self):
        from pathlib import Path
        import struct
        import tempfile
        import zlib
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'sheet.png'
            image = np.arange(32, dtype=np.uint8).reshape(4, 8)
            write_png(path, image)
            data = path.read_bytes()
            self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
            offset, chunks = 8, {}
            while offset < len(data):
                length = struct.unpack('>I', data[offset:offset+4])[0]
                kind, payload = data[offset+4:offset+8], data[offset+8:offset+8+length]
                crc = struct.unpack('>I', data[offset+8+length:offset+12+length])[0]
                self.assertEqual(crc, zlib.crc32(kind+payload))
                chunks[kind] = payload
                offset += length+12
            self.assertEqual(struct.unpack('>IIBBBBB', chunks[b'IHDR']), (8, 4, 8, 0, 0, 0, 0))
            decoded = np.frombuffer(zlib.decompress(chunks[b'IDAT']), dtype=np.uint8).reshape(4, 9)
            self.assertTrue(np.all(decoded[:, 0] == 0))
            np.testing.assert_array_equal(decoded[:, 1:], image)
            with self.assertRaises(FileExistsError):
                write_png(path, image)
            self.assertEqual(path.read_bytes(), data)

    def test_shared_window_keeps_brightness_difference_visible(self):
        source = np.tile(np.linspace(.1, .5, 256, dtype=np.float32), (256, 1))
        window = display_window(source)
        first, brighter = gray_view(source, window), gray_view(source+.02, window)
        self.assertGreater(float(brighter.mean()), float(first.mean()))
        self.assertEqual(window, display_window(source))
        self.assertTrue(np.all(np.diff(first[100].astype(np.int16)) >= 0))

    def test_flat_signed_values_and_nonfinite_rejection(self):
        flat = np.full((256, 256), -.01, dtype=np.float32)
        window = display_window(flat)
        self.assertAlmostEqual(window['high']-window['low'], .02)
        self.assertEqual(gray_view(flat, window).shape, flat.shape)
        flat[1, 1] = np.nan
        with self.assertRaises(ValueError):
            display_window(flat)
        with self.assertRaises(ValueError):
            gray_view(flat, window)


if __name__ == '__main__':
    unittest.main()
