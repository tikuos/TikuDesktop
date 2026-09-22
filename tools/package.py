#!/usr/bin/env python3
# Tiku Operating System v0.06
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Stage a relocatable desktop distribution without development binaries."""
from pathlib import Path
import argparse
import json
import os
import plistlib
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SYSTEM = ('TikuDesktop', 'TikuTracker', 'tiku-run', 'tiku-desk')
LAUNCHERS = ('TikuDesktop', 'TikuTracker', 'tiku-desk')
RESOURCE_DIRS = ('system', 'applications', 'sounds', 'help', 'strings')
HELPERS = {'basic': ('tiku-basic',), 'editor': ('tiku-edit',),
           'terminal': ('tiku-term',)}


def manifests(root=ROOT):
    """Read the production source inventory, not stale build directories."""
    result = {}
    for manifest in sorted((root / 'apps').glob('*/*.app')):
        fields = dict(line.split('=', 1) for line in manifest.read_text().splitlines()
                      if '=' in line and not line.startswith('#'))
        if fields.get('category') == 'demos':
            continue
        for key in ('id', 'module'):
            value = fields.get(key, '')
            if not value or value in ('.', '..') or '/' in value or '\\' in value:
                raise RuntimeError(f'Invalid {key} in {manifest}')
        if fields['id'] in result:
            raise RuntimeError('Duplicate application ID: ' + fields['id'])
        result[fields['id']] = (manifest, fields['module'])
    if not result:
        raise RuntimeError('No production application manifests')
    return result


def replace_generated(stage, destination, marker='.tiku-package.json'):
    """Publish a staged package, preserving unrecognized destinations."""
    previous = destination.with_name('.' + destination.name + '.previous')
    if destination.is_symlink():
        raise RuntimeError('Refusing to replace a symlink distribution')
    if destination.exists() and not (destination / marker).is_file():
        raise RuntimeError(f'{destination} is not a generated package; preserve it first')
    if previous.exists() or previous.is_symlink():
        raise RuntimeError(f'{previous} remains; preserve it before packaging')
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


def launchers(destination, root=ROOT):
    """Install the same resource-aware entry points in builds and packages."""
    for name in LAUNCHERS:
        target = destination / name
        shutil.copyfile(root / 'tools/runtime-launch.sh', target)
        target.chmod(0o755)


def resources(destination, root=ROOT):
    """Copy runtime resources without reference artwork or source generators."""
    destination.mkdir(parents=True, exist_ok=True)
    for name in RESOURCE_DIRS:
        source = root / 'assets' / name
        shutil.copytree(source, destination / name, dirs_exist_ok=True)


def package(root=ROOT):
    """Replace only a distribution carrying the generated-package marker."""
    parent = root / 'dist'
    parent.mkdir(exist_ok=True)
    destination = parent / 'TikuDesktop'
    if destination.is_symlink():
        raise RuntimeError('Refusing to replace a symlink distribution')
    if destination.exists() and not (destination / '.tiku-package.json').is_file():
        raise RuntimeError('dist/TikuDesktop is not a generated package; preserve it first')
    with tempfile.TemporaryDirectory(prefix='.tiku-package-', dir=parent) as temp:
        stage = Path(temp) / 'TikuDesktop'
        (stage / 'system').mkdir(parents=True)
        (stage / 'apps').mkdir()
        for name in SYSTEM:
            shutil.copy2(root / 'build/runtime/system' / name, stage / 'system' / name)
        inventory = manifests(root)
        for app_id, (manifest, module) in inventory.items():
            source = root / 'build/runtime/apps' / app_id
            bundle = stage / 'apps' / app_id
            bundle.mkdir()
            shutil.copy2(manifest, bundle / 'app.manifest')
            shutil.copy2(source / module, bundle / module)
            for helper in HELPERS.get(app_id, ()):
                if (source / helper).is_file():
                    shutil.copy2(source / helper, bundle / helper)
        resources(stage / 'assets', root)
        launchers(stage, root)
        (stage / '.tiku-package.json').write_text(
            json.dumps({'version': 1, 'apps': list(inventory)}, indent=2) + '\n')
        validate(stage, inventory, root)
        replace_generated(stage, destination)
    print('Package: dist/TikuDesktop (system, apps, assets; no demos or test binaries)')


def validate(package_root, inventory, root=ROOT):
    """Validate staged or installed output against the source-owned inventory."""
    if package_root.is_symlink():
        raise RuntimeError('Unexpected symlink package: ' + str(package_root))
    executables = ['system/' + name for name in SYSTEM] + list(LAUNCHERS)
    for app_id, helpers in HELPERS.items():
        if app_id in inventory:
            executables += [f'apps/{app_id}/{name}' for name in helpers
                            if (package_root / 'apps' / app_id / name).exists()]
    for name in executables:
        path = package_root / name
        if not path.is_file() or not os.access(path, os.X_OK):
            raise RuntimeError('Missing or non-executable program: ' + name)
    try:
        metadata = json.loads((package_root / '.tiku-package.json').read_text())
    except (OSError, ValueError) as error:
        raise RuntimeError('Invalid package metadata') from error
    expected = {'version': 1, 'apps': list(inventory)}
    if metadata != expected:
        raise RuntimeError('Package application inventory differs from source manifests')
    actual = {path.name for path in (package_root / 'apps').iterdir()}
    if actual != set(inventory):
        raise RuntimeError('Unexpected or missing application bundles')
    for app_id, (manifest, module) in inventory.items():
        bundle = package_root / 'apps' / app_id
        if not (bundle / module).is_file():
            raise RuntimeError('Missing packaged module: ' + module)
        if (bundle / 'app.manifest').read_bytes() != manifest.read_bytes():
            raise RuntimeError('Stale packaged manifest: ' + app_id)
        allowed = {'app.manifest', module, *HELPERS.get(app_id, ())}
        if {path.name for path in bundle.iterdir()} - allowed:
            raise RuntimeError('Unexpected file in application bundle: ' + app_id)
    for name in RESOURCE_DIRS:
        source = root / 'assets' / name
        if not source.is_dir() or not (package_root / 'assets' / name).is_dir():
            raise RuntimeError('Missing resource directory: ' + name)
        for path in source.rglob('*'):
            if path.is_file() and not (package_root / path.relative_to(root)).is_file():
                raise RuntimeError('Missing packaged resource: ' + str(path.relative_to(root)))
    for name in ('help/index.html', 'strings/en.xml'):
        if not (package_root / 'assets' / name).is_file():
            raise RuntimeError('Missing required resource: ' + name)
    for path in package_root.rglob('*'):
        if path.is_symlink():
            raise RuntimeError('Unexpected symlink in package: ' + str(path))
        if path.suffix in ('.o', '.a', '.c', '.h', '.py') or path.name.startswith('test_'):
            raise RuntimeError('Development file in package: ' + str(path))


def check(root=ROOT):
    """Check every declared executable and the resources needed outside the repo."""
    inventory = manifests(root)
    validate(root / 'dist/TikuDesktop', inventory, root)
    print(f'Package complete: {len(inventory)} apps and bundled resources')


def mac_bundle(host, root=ROOT):
    """Wrap the production package in a Finder bundle with its own assets."""
    check(root)
    source = root / 'dist/TikuDesktop'
    destination = root / 'dist/TikuDesktop.app'
    alias = root / 'build/TikuDesktop.app'
    backup = root / 'build/TikuDesktop.app.legacy-backup'
    if alias.exists() and not alias.is_symlink() and (backup.exists() or backup.is_symlink()):
        raise RuntimeError(f'Preserve {backup} before migrating the old bundle')
    with tempfile.TemporaryDirectory(prefix='.tiku-mac-', dir=root / 'dist') as temp:
        stage = Path(temp) / 'TikuDesktop.app'
        executables = stage / 'Contents/MacOS'
        assets = stage / 'Contents/Resources'
        executables.mkdir(parents=True)
        assets.mkdir(parents=True)
        for name in SYSTEM:
            target = 'TikuDesktop-bin' if name == 'TikuDesktop' else name
            shutil.copy2(source / 'system' / name, executables / target)
        shutil.copytree(source / 'apps', assets / 'apps')
        shutil.copytree(source / 'assets', assets / 'assets')
        shutil.copy2(root / 'tools/bundle-launch.sh', executables / 'TikuDesktop')
        (executables / 'TikuDesktop').chmod(0o755)
        (assets / 'host').write_text(host + '\n')
        iconset = Path(temp) / 'TikuDesktop.iconset'
        iconset.mkdir()
        subprocess.run([str(root / 'build/tools/tiku-app-icon'), str(iconset)],
                       check=True, stdout=subprocess.DEVNULL)
        subprocess.run(['iconutil', '-c', 'icns', str(iconset), '-o',
                        str(assets / 'TikuDesktop.icns')], check=True)
        info = dict(CFBundleName='TikuDesktop', CFBundleDisplayName='TikuDesktop',
                    CFBundleIdentifier='org.tiku-os.desktop',
                    CFBundleExecutable='TikuDesktop', CFBundleIconFile='TikuDesktop',
                    CFBundlePackageType='APPL', CFBundleShortVersionString='1.0',
                    CFBundleVersion='1.0', NSHighResolutionCapable=True,
                    LSMinimumSystemVersion='11.0')
        (stage / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
        shutil.copy2(source / '.tiku-package.json', assets / '.tiku-package.json')
        replace_generated(stage, destination, 'Contents/Resources/.tiku-package.json')
    if alias.is_symlink():
        alias.unlink()
    elif alias.exists():
        alias.rename(backup)
        print(f'Previous build bundle preserved at {backup}')
    alias.symlink_to('../dist/TikuDesktop.app')
    print('Bundle: dist/TikuDesktop.app (build/TikuDesktop.app is a compatibility link)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('resources', 'package', 'check', 'mac'))
    parser.add_argument('--host', choices=('cocoa', 'x11'), default='cocoa')
    args = parser.parse_args()
    if args.action == 'resources':
        resources(ROOT / 'build/runtime/assets')
        launchers(ROOT / 'build/runtime')
    elif args.action == 'package':
        package()
    elif args.action == 'check':
        check()
    else:
        mac_bundle(args.host)
