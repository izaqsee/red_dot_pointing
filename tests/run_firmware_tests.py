"""Build the production config sources against a tiny EEPROM/Stream mock.

Uses CXX (or c++/g++/clang++) on Unix; cl.exe / installed VS Build Tools on Windows.
Artifacts live in a temporary directory. No device access or Flash writes.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [ROOT / "tests/firmware_config_test.cpp", ROOT / "tests/firmware_actions_test.cpp", ROOT / "tests/firmware_http_test.cpp", ROOT / "firmware/http_lwip/redpoint_httpd.cpp"] + [
    ROOT / "firmware/redpoint" / name
    for name in ("status_led.cpp", "config.cpp", "config_command.cpp", "config_http.cpp", "config_record.cpp", "config_storage.cpp", "button_action.cpp", "keyboard_mapping.cpp")
]
INCLUDES = [ROOT / "tests/firmware_stubs", ROOT / "firmware/redpoint"]


def main():
    compiler = os.environ.get("CXX") or next(
        (path for name in ("c++", "g++", "clang++", "cl") if (path := shutil.which(name))), None
    )
    vcvars = None
    if not compiler and os.name == "nt":
        vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
        if vswhere.exists():
            install = subprocess.check_output([
                str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-property", "installationPath"
            ], text=True).strip()
            if install:
                vcvars = Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
                compiler = "cl"
    if not compiler:
        raise SystemExit("C++ compiler required: set CXX or install Visual Studio C++ Build Tools / g++.")
    with tempfile.TemporaryDirectory(prefix="redpoint-tests-") as temporary:
        build = Path(temporary)
        binary = build / ("firmware-test.exe" if os.name == "nt" else "firmware-test")
        if Path(compiler).stem.lower() == "cl":
            args = [compiler, "/nologo", "/utf-8", "/EHsc", "/std:c++14", "/Dstrtok_r=strtok_s"]
            args += [f"/I{path}" for path in INCLUDES] + [str(path) for path in SOURCES] + [f"/Fe:{binary}"]
        else:
            args = [compiler, "-std=c++11", "-Wall", "-Wextra", "-pedantic"]
            args += [f"-I{path}" for path in INCLUDES] + [str(path) for path in SOURCES] + ["-o", str(binary)]
        if vcvars:
            # Paths come only from the local toolchain/repo; no user command text.
            script = build / "build.cmd"
            script.write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\n' + subprocess.list2cmdline(args) + "\n")
            subprocess.run(str(script), cwd=build, shell=True, check=True)
        else:
            subprocess.run(args, cwd=build, check=True)
        subprocess.run([str(binary)], cwd=build, check=True)


if __name__ == "__main__":
    main()
