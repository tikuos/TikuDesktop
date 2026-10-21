#!/usr/bin/env python3
# Version: 0.01
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Check independent app builds, SDK export and application installation."""
from pathlib import Path
from types import SimpleNamespace
import argparse
import ctypes
import importlib.util
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest

import export_sdk
import package as desktop_package

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('package_app', ROOT / 'apps/package_app.py')
package_app = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package_app)


def command(arguments, cwd, **kwargs):
    """Run a bounded build command and include its diagnostics on failure."""
    result = subprocess.run(arguments, cwd=cwd, text=True, capture_output=True,
                            timeout=180, **kwargs)
    if result.returncode:
        raise RuntimeError(' '.join(map(str, arguments)) + '\n' + result.stdout + result.stderr)
    return result.stdout


def load_module(module, expected_id, abi):
    """Resolve the production export and validate its ABI and descriptor ID."""
    class Export(ctypes.Structure):
        """The stable loader export prefix declared in tiku_app.h."""
        _fields_ = [('abi', ctypes.c_uint32), ('size', ctypes.c_uint32),
                    ('descriptor', ctypes.c_void_p)]
    library = ctypes.CDLL(str(module))
    exported = Export.in_dll(library, 'tiku_app_v1')
    assert exported.abi == abi and exported.size > 0 and exported.descriptor
    identity = ctypes.cast(exported.descriptor, ctypes.POINTER(ctypes.c_char_p))[0]
    descriptor_id = {'calc': 'tiku.calc', 'editor': 'org.tikuos.edit',
                     'write': 'org.tikuos.write', 'word': 'org.tikuos.word',
                     'basic': 'org.tikuos.tikubasic', 'deck': 'org.tikuos.deck'}.get(expected_id, expected_id)
    assert identity.decode() == descriptor_id, (module, identity)


class BuildTests(unittest.TestCase):
    """Exercise ownership and replacement rules in disposable directories."""

    def setUp(self):
        """Allocate a test tree outside the source checkout."""
        directory = tempfile.TemporaryDirectory(prefix='app-build-')
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

    def test_core_entry_point(self):
        """The desktop-only graph contains no application compilation."""
        output = command(['make', '-n', 'core'], ROOT)
        self.assertFalse(any(' -c ' in line and ' apps/' in line
                             for line in output.splitlines()))
        self.assertNotIn('-C .. all', (ROOT / 'apps/Makefile').read_text())

    def test_core_without_application_sources(self):
        """The desktop graph resolves when the application subtree is absent."""
        for name in ('Makefile',):
            shutil.copy2(ROOT / name, self.root / name)
        for name in ('mk', 'system', 'kits', 'tools', 'assets', 'tests'):
            (self.root / name).symlink_to(ROOT / name, target_is_directory=True)
        output = command(['make', '-n', 'core'], self.root)
        self.assertNotIn(' -c apps/', output)

    def test_sdk_without_optional_kits(self):
        """An SDK exports the declared kit profile without optional archives."""
        names = [name for name in export_sdk.ORDER if name not in ('document', 'deck')]
        libraries = []
        for name in names:
            directory = self.root / 'kits' / name
            directory.mkdir(parents=True)
            (directory / (name + '.h')).write_text('')
            library = self.root / ('libtiku_' + name + '.a')
            library.write_bytes(b'archive fixture')
            libraries.append(library)
        (self.root / 'kits/application/tiku_app.h').write_text('#define TIKU_APP_ABI 5u\n')
        for relative in getattr(desktop_package, 'RESOURCE_NOTICES', {}).values():
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('License fixture\n')
        args = SimpleNamespace(output=self.root / 'sdk', copy=None,
                               libraries=libraries, cflags='', ldflags='', host='x11')
        export_sdk.export(args, self.root)
        metadata = json.loads((args.output / 'sdk.json').read_text())
        self.assertEqual(metadata['kit_names'].split(), names)
        self.assertEqual(metadata['libraries'], [path.name for path in libraries])

    def test_private_header_graph_is_excluded(self):
        """A transitively private header cannot enter the SDK include directory."""
        headers = self.root / 'kits/support'
        headers.mkdir(parents=True)
        for name, content in {'public.h': '#include "types.h"\n', 'types.h': '',
                              'model_private.h': '',
                              'internal.h': '#include "model_private.h"\n',
                              'indirect.h': '#include "internal.h"\n'}.items():
            (headers / name).write_text(content)
        self.assertEqual(set(export_sdk.public_headers(self.root)), {'public.h', 'types.h'})

    def test_config_preserves_unchanged_timestamp(self):
        """Unchanged flags preserve object dependencies; changed flags update data."""
        path = self.root / 'config/flags'
        package_app.write_changed(path, b'-O2')
        before = path.stat().st_mtime_ns
        package_app.write_changed(path, b'-O2')
        self.assertEqual(path.stat().st_mtime_ns, before)
        package_app.write_changed(path, b'-O0')
        self.assertEqual(path.read_bytes(), b'-O0')

    def test_sdk_preserves_unknown_destination(self):
        """An SDK export cannot replace an unrelated directory."""
        destination = self.root / 'sdk'
        destination.mkdir()
        (destination / 'keep').write_text('user data')
        stage = self.root / 'stage'
        stage.mkdir()
        with self.assertRaises(RuntimeError):
            export_sdk.publish(stage, destination)
        self.assertEqual((destination / 'keep').read_text(), 'user data')

    def test_bundle_preserves_unknown_destination(self):
        """Application installation cannot replace an unrecognized bundle."""
        destination = self.root / 'app'
        destination.mkdir()
        stage = self.root / 'stage'
        stage.mkdir()
        with self.assertRaises(RuntimeError):
            package_app.replace(stage, destination, package_app.MARKER)

    def test_clean_preserves_unmarked_directory(self):
        """Application cleanup refuses unrelated contents, including config folders."""
        (self.root / 'config').mkdir()
        (self.root / 'keep').write_text('user data')
        with self.assertRaises(RuntimeError):
            package_app.clean(self.root)
        self.assertEqual((self.root / 'keep').read_text(), 'user data')

    def test_prepare_preserves_unmarked_directory(self):
        """The build refuses to claim a nonempty user directory."""
        (self.root / 'keep').write_text('user data')
        with self.assertRaises(RuntimeError):
            package_app.prepare(self.root)
        self.assertEqual((self.root / 'keep').read_text(), 'user data')

    def test_core_package_has_no_apps(self):
        """A core-only package ignores production app manifests and modules."""
        import test_layout
        fixture = test_layout.LayoutTests()
        fixture.root = self.root
        fixture.fixture()
        desktop_package.package(self.root, core=True)
        desktop_package.check(self.root, core=True)
        self.assertEqual(list((self.root / 'dist/TikuDesktop-core/apps').iterdir()), [])
        self.assertTrue((self.root / 'build/runtime/apps/editor/Editor.so').exists())


def runtime_integration(temp):
    """Launch a detached-built app from an otherwise empty core distribution."""
    driver = ROOT / 'build/dev/TikuDesktop'
    if not driver.is_file() or not (ROOT / 'build/runtime/system/TikuDesktop').is_file():
        raise RuntimeError('Build core and the development desktop before the runtime check')
    fixture = temp / 'core-fixture'
    (fixture / 'build/runtime').mkdir(parents=True)
    (fixture / 'build/runtime/system').symlink_to(ROOT / 'build/runtime/system')
    (fixture / 'assets').symlink_to(ROOT / 'assets')
    (fixture / 'tools').symlink_to(ROOT / 'tools')
    for name in getattr(desktop_package, 'RESOURCE_NOTICES', {}).values():
        target = fixture / name
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, target)
    desktop_package.package(fixture, core=True)
    runtime = fixture / 'dist/TikuDesktop-core'
    shutil.copytree(temp / 'installed/apps/calc', runtime / 'apps/calc')
    home = temp / 'desktop-home'
    (home / 'Desktop').mkdir(parents=True)
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith('TIKU_')}
    environment.update(HOME=str(home), DISPLAY='',
                       XDG_CONFIG_HOME=str(home / '.config'),
                       XDG_DATA_HOME=str(home / '.local/share'))
    environment.update(TIKU_APPS=str(runtime / 'apps'),
                       TIKU_HELP=str(runtime / 'assets/help'),
                       TIKU_STRINGS=str(runtime / 'assets/strings'))
    process = subprocess.Popen([str(driver), '-script',
        str(ROOT / 'tests/integration/app-sdk.script')],
        cwd=temp, env=environment, start_new_session=True)
    try:
        result = process.wait(timeout=60)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
        raise RuntimeError('Independent application runtime test timed out') from None
    if result:
        raise RuntimeError('Independent application runtime test failed')
    print('Scripted desktop launched and used a detached-built Calculator from the core package', flush=True)


def integration():
    """Build exported app sources against a relocated SDK and load the modules."""
    with tempfile.TemporaryDirectory(prefix='detached-apps-') as temp:
        temp = Path(temp)
        original = temp / 'original-sdk'
        relocated = temp / 'relocated-sdk'
        command([sys.executable, str(ROOT / 'tools/export_sdk.py'),
                 '--copy', str(ROOT / 'build/sdk'), '--output', str(original)], temp)
        original.rename(relocated)
        sdk = json.loads((relocated / 'sdk.json').read_text())
        assert not any(relocated.rglob('*_private.h'))
        assert not any(relocated.rglob('*.c'))
        assert str(ROOT) not in (relocated / 'sdk.mk').read_text()
        environment = dict(os.environ, PKG_CONFIG_PATH=str(relocated / 'lib/pkgconfig'))
        pc = next((relocated / 'lib/pkgconfig').glob('*.pc')).stem
        flags = command(['pkg-config', '--cflags', '--libs', pc], temp, env=environment)
        assert str(relocated) in flags and str(ROOT) not in flags
        selected = ['calc', 'editor', 'doc', 'marker', 'write', 'basic']
        if (ROOT / 'apps/word/word.app').exists():
            selected += ['word', 'deck']
        inventory = desktop_package.manifests(ROOT)
        selected = [app for app in selected if app in inventory]
        for app in selected:
            source_parent = temp / 'sources'
            command(['make', '-s', '-C', str(ROOT / 'apps'), 'source', 'APP=' + app,
                     'APP_SOURCE_DIR=' + str(source_parent)], temp)
            source = source_parent / app
            output = command(['make', '-j4', 'SDK=' + str(relocated), 'package'], source)
            assert str(ROOT / 'kits') not in output and str(ROOT / 'system') not in output
            metadata = json.loads((source / 'build/packages' / app / package_app.MARKER).read_text())
            module = source / 'build/apps' / app / metadata['module']
            load_module(module, app, sdk['abi'])
            for dependency in (source / 'build/obj').rglob('*.d'):
                assert str(ROOT) not in dependency.read_text(), dependency
            before = module.stat().st_mtime_ns
            command(['make', '-j4', 'SDK=' + str(relocated)], source)
            assert module.stat().st_mtime_ns == before, 'Unchanged app was relinked'
            if app == 'calc':
                command(['make', '-j4', 'SDK=' + str(relocated), 'CFLAGS=-O0 -g'], source)
                assert module.stat().st_mtime_ns > before, 'Flag change did not relink app'
            # A real compiler depfile must trigger a rebuild after a header edit.
            if app == 'editor':
                header = source / 'editor/tiku_editor_private.h'
                header.write_text(header.read_text() + '\n')
                command(['make', '-j4', 'SDK=' + str(relocated)], source)
                assert module.stat().st_mtime_ns > before, 'Header change did not relink app'
            if app == 'basic':
                firmware = ROOT.parent
                if (firmware / 'basic/tiku_basic.c').is_file():
                    command(['make', '-j4', 'SDK=' + str(relocated), 'TIKUOS=' + str(firmware), 'package'], source)
                    helper = source / 'build/packages/basic/tiku-basic'
                    assert helper.is_file(), 'BASIC package is missing its explicit helper'
                    command(['sh', str(ROOT / 'apps/basichost/check_basic_host.sh'), str(helper)], temp)
            destination = temp / 'installed/apps'
            command(['make', 'SDK=' + str(relocated), 'install',
                     'APP_INSTALL_DIR=' + str(destination)], source)
            installed = destination / app
            load_module(installed / metadata['module'], app, sdk['abi'])
            assert (installed / 'licenses').is_dir()
            print(f'Detached SDK build, ABI load, incremental rebuild and install: {app}', flush=True)
            shutil.rmtree(source)
        runtime_integration(temp)
        print('Detached application builds passed', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--integration', action='store_true')
    parser.add_argument('--runtime-only', action='store_true')
    args = parser.parse_args()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(BuildTests))
    if not result.wasSuccessful():
        sys.exit(1)
    if args.integration:
        integration()
    elif args.runtime_only:
        with tempfile.TemporaryDirectory(prefix='app-runtime-') as directory:
            temp = Path(directory)
            manifest, module = desktop_package.manifests(ROOT)['calc']
            package_app.package(SimpleNamespace(sdk=ROOT / 'build/sdk', manifest=manifest,
                module=ROOT / 'apps/build/apps/calc' / module, output=temp / 'installed/apps/calc'))
            runtime_integration(temp)
