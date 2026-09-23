"""Milestone A checks plus the final ELF's Configurator assets and frozen sources."""
import hashlib
import importlib.util
import json
from pathlib import Path
import runpy
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
target = HERE.parent
root = target.parents[1]
state = runpy.run_path(str(HERE / "check_build.py"))
symbols, build = state["symbols"], state["build"]
spec = importlib.util.spec_from_file_location("fsdata", root / "tools/generate_http_fsdata.py")
fsdata = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fsdata)
assert (build / "redpoint-http/fsdata_redpoint.c").read_text() == fsdata.generate()
for index, (name, mime) in enumerate(fsdata.ASSETS.items()):
    wire = symbols[f"data_{index}"]
    header, body = wire.split(b"\r\n\r\n", 1)
    assert body == (root / "configurator" / name).read_bytes(), name
    assert header.startswith(b"HTTP/1.0 200 OK")
    assert f"Content-Type: {mime}".encode() in header
    assert f"Content-Length: {len(body)}".encode() in header
assert "root_file" in symbols
for name in ("httpd_post_begin", "httpd_post_receive_data", "httpd_post_finished", "redpoint_config_cdc_task"):
    assert name in symbols, name
assert sum("executeConfigCommand" in name for name in symbols) == 1
print("PASS: generated fsdata and final ELF assets match current Configurator; HTTP/CDC share one command core")
for name, digest in json.loads((target / "milestone_a_freeze.json").read_text()).items():
    assert hashlib.sha256((target / name).read_bytes()).hexdigest() == digest, name
print("PASS: frozen USB/HID descriptors, USB configuration and frontend sources unchanged")
