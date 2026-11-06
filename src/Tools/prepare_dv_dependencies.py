"""Build and restore the pinned FFmpeg inputs for DV processing from a local FFmpeg checkout.

Run in a Visual Studio x64 tools prompt after fetch_dependencies.py. This does
not install or register filters.
"""
import argparse
import json
import pathlib
import shutil
import subprocess
import pefile

from build_ffmpeg import build

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ffmpeg', type=pathlib.Path, required=True)
    args = parser.parse_args()
    source = args.ffmpeg.resolve()
    lock = json.loads((ROOT / 'deps/dv-ffmpeg.lock.json').read_text())
    commit = subprocess.check_output(['git', '-c', f'safe.directory={source.as_posix()}', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if commit != lock['commit']:
        raise ValueError('FFmpeg source does not match the pinned commit')
    runtime = build(source)
    target = ROOT / 'deps/lavffmpeg'
    for name in ('include', 'bin', 'lib', 'licenses'):
        shutil.rmtree(target / name, ignore_errors=True)
        (target / name).mkdir(parents=True)
    for directory in ('libavcodec', 'libavformat', 'libavutil'):
        for path in (source / directory).rglob('*.h'):
            dest = target / 'include' / path.relative_to(source)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dest)
    # The module's public-header build targets little-endian Windows x64.
    (target / 'include/libavutil/avconfig.h').write_text('#define AV_HAVE_BIGENDIAN 0\n#define AV_HAVE_FAST_UNALIGNED 1\n')
    libraries = []
    for library in lock['libraries']:
        name = pathlib.Path(library).name
        shutil.copy2(runtime / name, target / 'bin' / name)
        libraries.append(target / 'bin' / name)
    libraries.append(ROOT / 'build/bin/libdovi.dll')
    for dll in libraries:
        with pefile.PE(str(dll)) as pe:
            names = [entry.name.decode('ascii') for entry in pe.DIRECTORY_ENTRY_EXPORT.symbols if entry.name]
        definition = target / 'lib' / (dll.stem + '.def')
        definition.write_text('LIBRARY ' + dll.name + '\nEXPORTS\n' + '\n'.join(names) + '\n')
        subprocess.run(['lib', '/nologo', '/machine:x64', '/def:' + str(definition), '/out:' + str(definition.with_suffix('.lib'))], check=True)
    # The build is LGPL: no GPL components are enabled.
    for name in ('COPYING.LGPLv2.1', 'COPYING.LGPLv3', 'LICENSE.md'):
        if (source / name).exists():
            shutil.copy2(source / name, target / 'licenses' / name)
    print('Prepared pinned DV processing dependencies:', target)

if __name__ == '__main__':
    main()
