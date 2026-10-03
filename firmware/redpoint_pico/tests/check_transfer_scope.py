"""Check this change against remote main 6f83848, not obsolete milestone snapshots."""
from pathlib import Path
import subprocess
import runpy
import re
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASE = "6f83848b0bd3bdb878b55d28ce602a56b5175379"
runpy.run_path(str(HERE / "check_build.py"))
allowed = {
    "firmware/redpoint/config.h", "firmware/redpoint/config_command.cpp",
    "firmware/redpoint/config_record.h", "firmware/redpoint/config_record.cpp",
    "firmware/redpoint_pico/src/input_runtime.cpp",
    "firmware/redpoint_pico/src/input_runtime.h",
    "firmware/redpoint_pico/README.md",  # Evaluation wiring documentation only.
}
def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)
def normalized(data):
    return data.replace(b"\r\n", b"\n")
for name in git("ls-tree", "-r", "--name-only", BASE, "firmware").decode().splitlines():
    if "/tests/" not in name and name not in allowed:
        assert normalized((ROOT / name).read_bytes()) == normalized(git("show", BASE+":"+name)), name
# Only the evaluation GPIO constants/comments may differ in the input API header.
name = "firmware/redpoint_pico/src/input_runtime.h"
before = normalized(git("show", BASE+":"+name)).decode()
expected = before.replace(
    "enum { REDPOINT_CLK = 12, REDPOINT_DATA = 13, REDPOINT_LEFT = 3,\n"
    "       REDPOINT_MIDDLE = 2, REDPOINT_RIGHT = 4, REDPOINT_LED = 23 };",
    "// Authoritative GPIO assignment for the 3.3 V evaluation unit.\n"
    "// Physical button indices stay 0=Left, 1=Middle, 2=Right.\n"
    "enum { REDPOINT_CLK = 20, REDPOINT_DATA = 21, REDPOINT_LEFT = 4,\n"
    "       REDPOINT_MIDDLE = 5, REDPOINT_RIGHT = 3, REDPOINT_LED = 23 };")
assert (ROOT / name).read_text() == expected, name
platform = (ROOT / "firmware/redpoint_pico/src/platform_io.cpp").read_text()
pins = re.search(r"const uint pins\[\]\s*=\s*\{([^}]+)\}", platform)
assert pins and [v.strip() for v in pins[1].split(",")] == [
    "REDPOINT_LEFT", "REDPOINT_MIDDLE", "REDPOINT_RIGHT"]
assert "index < 3 && !gpio_get(pins[index])" in platform
print("PASS: evaluation CLK=20 DATA=21; button indices 0/1/2 map to Left=4/Middle=5/Right=3; LED=23")
name = "firmware/redpoint_pico/src/input_runtime.cpp"
before = normalized(git("show", BASE+":"+name)).decode()
after = (ROOT / name).read_text().replace('#include "input_response.h"\n', '')
assert before.split("void move(")[0] == after.split("void move(")[0]
assert before[before.index('extern "C" void redpoint_input_edge'): ] == after[after.index('extern "C" void redpoint_input_edge'): ]
name = "configurator/app.js"
before = normalized(git("show", BASE+":"+name)).decode()
after = (ROOT / name).read_text()
for function in ("parseLine", "createLineReader", "createProtocol", "createHeartbeat", "createHttpTransport", "createHttpConnection", "createSerialTransport"):
    def body(source):
        start = source.index("  function " + function + "(")
        return source[start:source.index("\n  }",start)+4]
    assert body(before) == body(after), function
print("PASS: main freeze outside approved config/response/pin files; PS/2/buttons/mode/network/USB/LED/transport unchanged")
