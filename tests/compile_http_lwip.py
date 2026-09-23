"""Compile the HTTP adapter/core and actual lwIP httpd/fs against a built SDK example.

Usage: python tests/compile_http_lwip.py path/to/compile_commands.json
Only temporary object files are produced. No linking, uploads or SDK edits.
"""
from pathlib import Path
import ctypes
import importlib.util
import json
import os
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]


def split_command(command):
    if os.name != 'nt':
        return shlex.split(command)
    count = ctypes.c_int()
    split = ctypes.windll.shell32.CommandLineToArgvW
    split.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    split.restype = ctypes.POINTER(ctypes.c_wchar_p)
    pointer = split(command, ctypes.byref(count))
    try:
        return [pointer[i] for i in range(count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree(ctypes.cast(pointer, ctypes.c_void_p))


def main():
    commands = json.loads(Path(sys.argv[1]).read_text())
    entry = next(c for c in commands if c['file'].replace('\\', '/').endswith('/apps/http/fs.c'))
    args = entry.get('arguments') or split_command(entry['command'])
    compiler = args[0]
    flags = []
    i = 1
    while i < len(args):
        if args[i] in ('-o', '-c'):
            i += 2
        else:
            flags.append(args[i]); i += 1
    spec = importlib.util.spec_from_file_location('fsdata', ROOT / 'tools/generate_http_fsdata.py')
    fsdata = importlib.util.module_from_spec(spec); spec.loader.exec_module(fsdata)
    with tempfile.TemporaryDirectory(prefix='redpoint-http-build-') as tmp:
        build = Path(tmp)
        (build / 'fsdata_redpoint.c').write_text(fsdata.generate(), encoding='ascii')
        options = ['-DLWIP_HTTPD_SUPPORT_POST=1', '-DLWIP_HTTPD_CUSTOM_FILES=1',
                   '-include', str(ROOT / 'firmware/http_lwip/redpoint_http_opts.h'),
                   '-DLWIP_HTTPD_DYNAMIC_HEADERS=0', '-DLWIP_HTTPD_DYNAMIC_FILE_READ=0',
                   '-DHTTPD_PRECALCULATED_CHECKSUM=0', '-DHTTPD_FSDATA_FILE="fsdata_redpoint.c"',
                   f'-I{build}', f'-I{ROOT / "firmware/redpoint"}']
        for name in ('fs.c', 'httpd.c'):
            source = Path(entry['file']).with_name(name)
            subprocess.run([compiler, *flags, *options, '-c', str(source), '-o', str(build / (name + '.o'))],
                           cwd=entry['directory'], check=True)
        cppflags = [f for f in flags if f not in ('-Wstrict-prototypes', '-Werror-implicit-function-declaration', '-Wmissing-prototypes')]
        for source in ('firmware/http_lwip/redpoint_httpd.cpp', 'firmware/redpoint/config_command.cpp', 'firmware/redpoint/config_http.cpp'):
            subprocess.run([compiler.replace('gcc', 'g++'), *cppflags, *options, '-std=c++17', '-c', str(ROOT / source),
                            '-o', str(build / (Path(source).name + '.o'))], cwd=entry['directory'], check=True)
    print('PASS: actual lwIP httpd/fs, generated assets and RedPoint adapter/core compile (objects only)')


if __name__ == '__main__':
    main()
