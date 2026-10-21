#!/usr/bin/env python3
# Version: 0.01
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Export application sources and install independently built bundles."""
from pathlib import Path
import argparse
import json
import shutil
import sys
import tempfile

MARKER = '.tiku-app.json'
SOURCE_MARKER = '.tiku-app-source.json'
BUILD_MARKER = '.tiku-app-build.json'


def write_changed(path, data):
    """Preserve timestamps when generated command data has not changed."""
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def replace(stage, destination, marker):
    """Replace a generated tree while preserving an unrecognized destination."""
    previous = destination.with_name('.' + destination.name + '.previous')
    if destination.is_symlink() or previous.exists() or previous.is_symlink():
        raise RuntimeError(f'Cannot replace {destination}: symlink or previous output')
    if destination.exists() and not (destination / marker).is_file():
        raise RuntimeError(f'{destination} is not generated output')
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


def manifest_fields(path):
    """Read an application manifest and validate its local file names."""
    fields = dict(line.split('=', 1) for line in path.read_text().splitlines()
                  if '=' in line and not line.startswith('#'))
    for key in ('id', 'module'):
        value = fields.get(key, '')
        if not value or value in ('.', '..') or '/' in value or '\\' in value:
            raise RuntimeError(f'Invalid manifest {key}: {path}')
    return fields


def package(args):
    """Stage a module, manifest and the SDK's redistribution notices."""
    fields = manifest_fields(args.manifest)
    if args.module.name != fields['module'] or not args.module.is_file():
        raise RuntimeError('Module does not match the manifest')
    sdk = json.loads((args.sdk / 'sdk.json').read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.app-', dir=args.output.parent) as temp:
        stage = Path(temp) / fields['id']
        stage.mkdir()
        shutil.copy2(args.manifest, stage / 'app.manifest')
        shutil.copy2(args.module, stage / fields['module'])
        helpers = {'basic': ('tiku-basic',), 'editor': ('tiku-edit',),
                   'terminal': ('tiku-term',)}
        for helper in helpers.get(fields['id'], ()):
            if (args.module.parent / helper).is_file():
                shutil.copy2(args.module.parent / helper, stage / helper)
        shutil.copytree(args.sdk / 'licenses', stage / 'licenses')
        metadata = dict(version=1, id=fields['id'], module=fields['module'],
                        sdk=sdk['product'], sdk_version=sdk['version'], abi=sdk['abi'],
                        system=sdk['system'], machine=sdk['machine'])
        (stage / MARKER).write_text(json.dumps(metadata, indent=2) + '\n')
        replace(stage, args.output, MARKER)
    print('Application package:', args.output)


def source(args):
    """Export one application's code and its reusable application libraries."""
    fields = manifest_fields(args.manifest)
    if fields['id'] != args.app:
        raise RuntimeError('Source application does not match its manifest')
    root = args.root.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.source-', dir=args.output.parent) as temp:
        stage = Path(temp) / args.app
        stage.mkdir()
        for name in ('catalog.mk', 'rules.mk', 'build.mk', 'package_app.py', 'firmware.mk'):
            if not (root / name).is_file():
                continue
            shutil.copy2(root / name, stage / name)
        (stage / 'Makefile').write_text(f'APP ?= {args.app}\n'
            'APPS_SOURCE := $(abspath .)\ninclude $(APPS_SOURCE)/build.mk\n')
        for directory in args.directories:
            directory = directory.resolve()
            if not directory.is_relative_to(root) or directory == root:
                raise RuntimeError('Application source must be under apps/')
            target = stage / directory.relative_to(root)
            target.mkdir(parents=True, exist_ok=True)
            for path in directory.iterdir():
                if path.is_file() and (path.suffix in ('.c', '.h', '.inl', '.app', '.mk')
                                       or path.name.startswith(('Makefile', 'LICENSE'))):
                    shutil.copy2(path, target / path.name)
        (stage / SOURCE_MARKER).write_text(json.dumps(dict(version=1, id=args.app)) + '\n')
        replace(stage, args.output, SOURCE_MARKER)
    print('Application sources:', args.output)


def install(args):
    """Install a generated bundle into the desktop's application directory."""
    metadata = json.loads((args.bundle / MARKER).read_text())
    fields = manifest_fields(args.bundle / 'app.manifest')
    if metadata['id'] != fields['id'] or metadata['module'] != fields['module']:
        raise RuntimeError('Bundle metadata does not match its manifest')
    if args.output.name != fields['id']:
        raise RuntimeError('Installation directory must match the application ID')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.install-', dir=args.output.parent) as temp:
        stage = Path(temp) / fields['id']
        shutil.copytree(args.bundle, stage, symlinks=False)
        replace(stage, args.output, MARKER)
    print('Installed application:', args.output)


def prepare(path):
    """Claim a new build directory and refuse unrelated existing contents."""
    if path.is_symlink():
        raise RuntimeError(f'{path} is a symlink')
    path.mkdir(parents=True, exist_ok=True)
    marker = path / BUILD_MARKER
    if marker.is_symlink():
        raise RuntimeError('Application build marker is a symlink')
    if not marker.exists():
        if any(path.iterdir()) and not marker.exists():
            raise RuntimeError(f'{path} is not an application build directory')
        marker.write_text('{"version": 1}\n')


def clean(path):
    """Remove a directory carrying the application build marker."""
    if not path.exists():
        return
    if path.is_symlink() or not (path / BUILD_MARKER).is_file():
        raise RuntimeError(f'{path} is not an application build directory')
    shutil.rmtree(path)


def main():
    """Dispatch configuration, source export, packaging and installation."""
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='action', required=True)
    config = commands.add_parser('config')
    config.add_argument('path', type=Path)
    config.add_argument('flags', nargs=argparse.REMAINDER)
    preparation = commands.add_parser('prepare')
    preparation.add_argument('path', type=Path)
    cleanup = commands.add_parser('clean')
    cleanup.add_argument('path', type=Path)
    bundle = commands.add_parser('package')
    for name in ('sdk', 'manifest', 'module', 'output'):
        bundle.add_argument('--' + name, type=Path, required=True)
    export = commands.add_parser('source')
    for name in ('root', 'manifest', 'output'):
        export.add_argument('--' + name, type=Path, required=True)
    export.add_argument('--app', required=True)
    export.add_argument('--directories', type=Path, nargs='+', required=True)
    installer = commands.add_parser('install')
    for name in ('bundle', 'output'):
        installer.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.action == 'config':
        prepare(args.path.parent.parent)
        write_changed(args.path, json.dumps(args.flags).encode())
    elif args.action == 'prepare':
        prepare(args.path)
    elif args.action == 'clean':
        clean(args.path)
    else:
        {'package': package, 'source': source, 'install': install}[args.action](args)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        sys.exit(str(error))
