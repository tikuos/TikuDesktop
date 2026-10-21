#!/usr/bin/env python3
# Version: 0.01
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Export the application SDK without system or application implementations."""
from pathlib import Path
import argparse
import json
import platform
import re
import shlex
import shutil
import sys
import tempfile

import package as desktop_package

ROOT = Path(__file__).resolve().parents[1]
PRODUCT = 'TikuDesktop'
PC_NAME = 'tiku-sdk.pc'
INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)
ORDER = ('editor', 'application', 'translation', 'deck', 'document',
         'interface', 'storage', 'device', 'media', 'support')


def public_headers(root):
    """Select headers whose local include graph does not reach private headers."""
    headers = {}
    for path in (root / 'kits').glob('*/*.h'):
        if path.name in headers:
            raise RuntimeError('Duplicate SDK header: ' + path.name)
        headers[path.name] = path
    private = {name for name in headers if '_private' in name}
    dependencies = {name: set(INCLUDE.findall(path.read_text()))
                    for name, path in headers.items()}
    while True:
        rejected = {name for name in headers if dependencies[name] & private}
        if rejected <= private:
            break
        private |= rejected
    return {name: path for name, path in headers.items() if name not in private}


def sdk_flags(flags):
    """Remove build tuning and relocate the forced Darwin compatibility header."""
    result = []
    for flag in shlex.split(flags):
        if flag.startswith(('-O', '-g')):
            continue
        if flag.startswith('kits/'):
            flag = '$(SDK_ROOT)/include/' + Path(flag).name
        if str(ROOT) in flag:
            raise RuntimeError('SDK compiler flags contain a source checkout path')
        result.append(flag)
    return result


def publish(stage, destination):
    """Replace a recognized SDK and retain unrecognized destination contents."""
    previous = destination.with_name('.' + destination.name + '.previous')
    if destination.is_symlink() or previous.exists() or previous.is_symlink():
        raise RuntimeError('SDK output is a symlink or has a previous export')
    if destination.exists() and not (destination / 'sdk.json').is_file():
        raise RuntimeError(f'{destination} is not an exported SDK')
    if destination.exists():
        destination.rename(previous)
    try:
        stage.rename(destination)
    except OSError:
        if previous.exists():
            previous.rename(destination)
        raise
    if previous.exists():
        shutil.rmtree(previous)


def export(args, root=ROOT):
    """Stage public headers, PIC archives, notices and relocatable build metadata."""
    args.output = args.output.absolute()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.sdk-', dir=args.output.parent) as temp:
        stage = Path(temp) / 'sdk'
        if args.copy:
            if args.copy.resolve() == args.output.resolve():
                raise RuntimeError('SDK installation source and output are the same')
            json.loads((args.copy / 'sdk.json').read_text())
            shutil.copytree(args.copy, stage)
        else:
            (stage / 'include').mkdir(parents=True)
            (stage / 'lib/pkgconfig').mkdir(parents=True)
            (stage / 'licenses').mkdir()
            headers = public_headers(root)
            for name, source in headers.items():
                shutil.copy2(source, stage / 'include' / name)
            libraries = {}
            for library in args.libraries:
                if not library.is_file():
                    raise RuntimeError('Missing SDK library: ' + str(library))
                libraries[library.stem.removeprefix('libtiku_')] = library
                shutil.copy2(library, stage / 'lib' / library.name)
            order = tuple(k for k in ORDER if k in libraries)
            required = set(ORDER) - {'deck', 'document'}
            if set(libraries) - set(ORDER) or not required <= set(libraries):
                raise RuntimeError('SDK requires one PIC archive for each kit')
            for source in (root / 'kits').glob('*/LICENSE*'):
                if source.is_file():
                    target = stage / 'licenses' / source.parent.name
                    target.mkdir(exist_ok=True)
                    shutil.copy2(source, target / source.name)
            for name, source in getattr(desktop_package, 'RESOURCE_NOTICES', {}).items():
                shutil.copy2(root / source, stage / 'licenses' / name)
            for name in ('LICENSE', 'NOTICE'):
                if (root / name).is_file():
                    shutil.copy2(root / name, stage / 'licenses' / name)
            app_header = headers['tiku_app.h'].read_text()
            abi = int(re.search(r'#define\s+TIKU_APP_ABI\s+(\d+)', app_header)[1])
            metadata = dict(version='0.01', product=PRODUCT, abi=abi,
                            system=platform.system(), machine=platform.machine(),
                            host=args.host, kit_names=' '.join(order), libraries=[libraries[k].name for k in order])
            (stage / 'sdk.json').write_text(json.dumps(metadata, indent=2) + '\n')
            cflags = ['-I$(SDK_ROOT)/include', *sdk_flags(args.cflags)]
            ldflags = shlex.split(args.ldflags)
            if any(str(root) in flag for flag in ldflags):
                raise RuntimeError('SDK linker flags contain a source checkout path')
            config = ['# Application SDK configuration; paths follow this file.\n',
                      'SDK_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))\n']
            for name in ('version', 'abi', 'system', 'machine', 'host', 'kit_names'):
                config.append(f'SDK_{name.upper()} := {metadata[name]}\n')
            config += ['SDK_CFLAGS := ' + shlex.join(cflags) + '\n',
                       'SDK_LDFLAGS := ' + shlex.join(ldflags) + '\n']
            (stage / 'sdk.mk').write_text(''.join(config))
            pc = ['prefix=${pcfiledir}/../..\n', 'libdir=${prefix}/lib\n',
                  'includedir=${prefix}/include\n', 'Name: ' + PRODUCT + ' SDK\n',
                  'Description: Native application kits\n', 'Version: 0.01\n',
                  'Cflags: ' + shlex.join(cflags).replace('$(SDK_ROOT)', '${prefix}') + '\n',
                  'Libs: -L${libdir} ' + ' '.join('-ltiku_' + k for k in order)
                  + ' ' + shlex.join(ldflags) + '\n']
            (stage / 'lib/pkgconfig' / PC_NAME).write_text(''.join(pc))
        publish(stage, args.output)
    print('Application SDK:', args.output)


def main():
    """Export from built archives or install an existing exported SDK."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--copy', type=Path)
    parser.add_argument('--cflags', default='')
    parser.add_argument('--ldflags', default='')
    parser.add_argument('--host', default='x11')
    parser.add_argument('--libraries', type=Path, nargs='*', default=[])
    export(parser.parse_args())


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        sys.exit(str(error))
