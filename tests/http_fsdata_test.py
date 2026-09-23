"""Verify embedded HTTP assets reproduce the frontend byte-for-byte."""
from pathlib import Path
import importlib.util
import re
import sys
import unittest

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fsdata', ROOT / 'tools/generate_http_fsdata.py')
fsdata = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fsdata)


class FsdataTests(unittest.TestCase):
    def test_assets_headers_and_determinism(self):
        generated = fsdata.generate()
        self.assertEqual(generated, fsdata.generate())
        for index, (name, mime) in enumerate(fsdata.ASSETS.items()):
            array = re.search(r'data_%d\[\] = \{(.*?)\};' % index, generated, re.S).group(1)
            wire = bytes(int(n) for n in re.findall(r'\d+', array))
            header, body = wire.split(b'\r\n\r\n', 1)
            self.assertEqual(body, (ROOT / 'configurator' / name).read_bytes())
            self.assertIn(f'Content-Length: {len(body)}'.encode(), header)
            self.assertIn(f'Content-Type: {mime}'.encode(), header)
            self.assertIn(b'Cache-Control: no-store', header)
            self.assertIn(f'"/{name}"', generated)
        self.assertIn('#define FS_NUMFILES 5', generated)
        self.assertIn('(const unsigned char *)"/", data_0', generated)


if __name__ == '__main__':
    unittest.main()
