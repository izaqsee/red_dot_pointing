"""C.3 permits presentation changes only; C.2 firmware and validation are frozen."""
import hashlib
import json
from pathlib import Path
import runpy
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
runpy.run_path(str(HERE / 'check_milestone_c2.py'))
manifest = json.loads((HERE.parent / 'milestone_c2_freeze.json').read_text())
for name, digest in manifest['firmware'].items():
    assert hashlib.sha256((ROOT / name).read_bytes().replace(b'\r\n', b'\n')).hexdigest() == digest, name
source = (ROOT / 'configurator/app.js').read_text()
a = source.index('  function validConfig(')
b = source.index('\n  }', a) + 4
assert hashlib.sha256(source[a:b].encode()).hexdigest() == manifest['validConfig']
print('PASS: all tracked C.2 firmware sources/build definitions and validConfig unchanged')
