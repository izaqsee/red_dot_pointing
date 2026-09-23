"""C.2 schema/UI changes are allowed; C.1 transport and hardware remain frozen."""
import hashlib
import json
from pathlib import Path
import runpy
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
runpy.run_path(str(HERE / 'check_milestone_c.py'))
manifest = json.loads((HERE.parent / 'milestone_c1_freeze.json').read_text())
for name, digest in manifest['files'].items():
    assert hashlib.sha256((ROOT / name).read_bytes().replace(b'\r\n', b'\n')).hexdigest() == digest, name
source = (ROOT / 'configurator/app.js').read_text()
for name, digest in manifest['frontend'].items():
    start = source.index('  function ' + name + '(')
    end = source.index('\n  }', start) + 4
    assert hashlib.sha256(source[start:end].encode()).hexdigest() == digest, name
print('PASS: C.1 HID queue/recovery, Flash backend, command adapters, owners, LED and frontend transport/state machine frozen')
source = (ROOT / 'firmware/redpoint_pico/src/input_runtime.cpp').read_text()
a = source.index('extern "C" void redpoint_input_edge(')
b = source.index('\n}', a) + 2
assert hashlib.sha256(source[a:b].encode()).hexdigest() == manifest['ps2_frame']
print('PASS: PS/2 IRQ frame decoder unchanged')
