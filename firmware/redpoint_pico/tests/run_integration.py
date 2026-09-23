"""Host-execute Pico backend + actual lwIP TCP/httpd/fs, without any USB access.

Uses the source selection from the completed ARM build. On Windows uses the same
Visual Studio Build Tools discovery as the existing firmware host tests.
"""
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else HERE.parent / "build"
spec = importlib.util.spec_from_file_location("runner", ROOT / "tests/run_firmware_tests.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
commands = json.loads((build / "compile_commands.json").read_text())
lwip = [Path(c["file"]) for c in commands if "/lib/lwip/src/" in c["file"].replace("\\", "/")]
assert lwip
tinyusb = lwip[0].parents[4]
sources = [HERE / "native/integration.cpp", HERE.parent / "src/config_platform.cpp",
           HERE / "native/platform.cpp", HERE / "native/hardware_tests.cpp", HERE / "native/scroll_tests.cpp",
           HERE.parent / "src/config_cdc.cpp", HERE.parent / "src/input_runtime.cpp",
           HERE.parent / "src/hid_state.cpp", HERE.parent / "src/status_led.cpp",
           HERE.parent / "src/config_storage.cpp", ROOT / "firmware/http_lwip/redpoint_httpd.cpp"]
sources += [ROOT / "firmware/redpoint" / name for name in
            ("config_command.cpp", "config_http.cpp", "config_record.cpp", "button_action_codec.cpp", "button_action_state.cpp")]
sources += lwip
includes = [HERE / "native", HERE.parent / "src", ROOT / "firmware/redpoint",
            ROOT / "firmware/http_lwip",
            tinyusb / "src", tinyusb / "lib/lwip/src/include", build / "redpoint-http"]
defines = ["CFG_TUSB_MCU=OPT_MCU_RP2040", "CFG_TUSB_OS=OPT_OS_NONE",
           "LWIP_HTTPD_SUPPORT_POST=1", "LWIP_HTTPD_CUSTOM_FILES=1",
           "LWIP_HTTPD_DYNAMIC_HEADERS=0", "LWIP_HTTPD_DYNAMIC_FILE_READ=0",
           "HTTPD_PRECALCULATED_CHECKSUM=0", 'HTTPD_FSDATA_FILE="fsdata_redpoint.c"']
capture = build / "host-get-response.txt"
runner.main(sources, includes, defines, [ROOT / "configurator", capture])
print(f"GET response from real lwIP/Pico backend: {capture}")
