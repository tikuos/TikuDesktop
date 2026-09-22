# Tiku Operating System v0.06
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Regression checks for source ownership, build migration and packaging."""
from pathlib import Path
import os
import json
import plistlib
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import package
import prepare_build

ROOT = Path(__file__).resolve().parents[1]


class LayoutTests(unittest.TestCase):
    """Exercise generated layouts in disposable trees, never the user's build."""

    def setUp(self):
        """Allocate a tree whose name also exercises quoting of paths."""
        directory = tempfile.TemporaryDirectory(prefix='tiku layout ')
        self.addCleanup(directory.cleanup)
        # Resolved once: on macOS the temporary directory is itself under
        # a symlink, and a resolve() on one side of a comparison is not
        # the same path as the unresolved other side.
        self.root = Path(directory.name).resolve()

    def write(self, name, content='fixture', executable=False):
        """Create a single fixture and any missing parent directories."""
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        if executable:
            path.chmod(0o755)
        return path

    def fixture(self):
        """Install a minimal source inventory and its generated modules."""
        for name in package.SYSTEM:
            self.write('build/runtime/system/' + name, '#!/bin/sh\n'
                       'printf \'%s\\n\' "$TIKU_APPS" "$TIKU_HELP" '
                       '"$TIKU_STRINGS" "$@"\n', executable=True)
        self.write('apps/editor/editor.app', 'id=editor\nmodule=Editor.so\n'
                   'category=applications\n')
        self.write('build/runtime/apps/editor/Editor.so')
        self.write('build/runtime/apps/editor/tiku-edit', executable=True)
        self.write('build/runtime/apps/editor/test_unwanted.c')
        self.write('build/runtime/apps/stale/app.manifest',
                   'id=stale\nmodule=Stale.so\ncategory=applications\n')
        self.write('build/runtime/apps/stale/Stale.so')
        self.write('examples/apps/hello.app', 'id=hello\nmodule=hello.so\n'
                   'category=demos\n')
        self.write('build/runtime/apps/hello/hello.so')
        for name in ('system', 'applications', 'sounds', 'strings'):
            self.write(f'assets/{name}/resource.txt')
        self.write('assets/help/index.html', '<h1>Help</h1>')
        self.write('assets/strings/en.xml', '<strings/>')
        for name in ('runtime-launch.sh', 'bundle-launch.sh'):
            self.write('tools/' + name, (ROOT / 'tools' / name).read_text())

    def test_migration_preserves_contents_and_is_idempotent(self):
        """Old generated paths become aliases without losing object contents."""
        pairs = {'system/TikuDesktop': 'runtime/system/TikuDesktop',
                 'apps/basic/TikuBasic.so': 'runtime/apps/basic/TikuBasic.so',
                 'gen/strings.h': 'generated/strings.h',
                 'pic/kits/unit.o': 'obj/pic/kits/unit.o',
                 'pic/libtiku_support.a': 'lib/pic/libtiku_support.a',
                 'libtiku_support.a': 'lib/libtiku_support.a',
                 'icon-export': 'tools/icon-export'}
        for old in pairs:
            self.write('build/' + old, old)
        prepare_build.prepare(self.root)
        prepare_build.prepare(self.root)
        for old, new in pairs.items():
            self.assertEqual((self.root / 'build' / new).read_text(), old)
        # The root keeps the three launchers and nothing else of the old
        # layout: the moved trees leave no alias behind.
        for name in ('system', 'apps', 'gen', 'pic', 'icon-export',
                     'libtiku_support.a'):
            self.assertFalse((self.root / 'build' / name).exists())
            self.assertFalse((self.root / 'build' / name).is_symlink())
        for name in ('TikuDesktop', 'TikuTracker', 'tiku-desk'):
            self.assertTrue((self.root / 'build' / name).is_symlink())

    def test_retired_aliases_are_removed(self):
        """The earlier layout's aliases go, dangling or not; the trees stay."""
        (self.root / 'build/lib').mkdir(parents=True)
        (self.root / 'build/system').symlink_to('runtime/system')
        (self.root / 'build/pic').symlink_to('lib/pic')
        (self.root / 'build/libtiku_media.a').symlink_to('lib/libtiku_media.a')
        prepare_build.prepare(self.root)
        for name in ('system', 'pic', 'libtiku_media.a'):
            self.assertFalse((self.root / 'build' / name).is_symlink())
        self.assertTrue((self.root / 'build/runtime/system').is_dir())
        self.assertTrue((self.root / 'build/lib/pic').is_dir())

    def test_conflicting_migration_does_not_overwrite(self):
        """Ambiguous old and new output trees require an explicit resolution."""
        old = self.write('build/system/file', 'old')
        new = self.write('build/runtime/system/file', 'new')
        with self.assertRaises(RuntimeError):
            prepare_build.prepare(self.root)
        self.assertEqual(old.read_text(), 'old')
        self.assertEqual(new.read_text(), 'new')

    def test_parallel_manifest_build_waits_for_migration(self):
        """Real manifest rules must not populate the destination before migration."""
        self.fixture()
        shutil.copyfile(ROOT / 'Makefile', self.root / 'Makefile')
        shutil.copytree(ROOT / 'mk', self.root / 'mk')
        self.write('tests/kits/test_placeholder.c')
        self.write('tools/_prepare_build.py', (ROOT / 'tools/prepare_build.py').read_text())
        self.write('tools/prepare_build.py', 'import time\n'
                   'from _prepare_build import prepare\ntime.sleep(0.2)\nprepare()\n')
        shutil.rmtree(self.root / 'build/runtime')
        self.write('build/apps/keep.txt', 'preserved')
        self.write('build/apps/Bench.so', 'obsolete flat module')
        targets = {}
        for source in list((ROOT / 'apps').glob('*/*.app')) + list(
                (ROOT / 'examples/apps').rglob('*.app')):
            content = source.read_text()
            self.write(str(source.relative_to(ROOT)), content)
            app_id = re.search(r'^id=(.+)$', content, re.M)[1]
            targets[f'build/runtime/apps/{app_id}/app.manifest'] = content
        result = subprocess.run(['make', '-j8', 'sweep-legacy', *targets],
                                cwd=self.root, text=True, capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / 'build/runtime/apps/keep.txt').read_text(), 'preserved')
        self.assertFalse((self.root / 'build/runtime/apps/Bench.so').exists())
        for target, content in targets.items():
            self.assertEqual((self.root / target).read_text(), content)

    def test_alias_does_not_replace_a_regular_file(self):
        """A manually installed launcher is not silently replaced."""
        path = self.write('build/TikuDesktop', 'keep')
        with self.assertRaises(RuntimeError):
            prepare_build.prepare(self.root)
        self.assertEqual(path.read_text(), 'keep')

    def test_migration_preserves_dangling_destination_link(self):
        """Even a dangling destination is an occupied path, not spare storage."""
        source = self.write('old.a', 'keep')
        destination = self.root / 'new.a'
        destination.symlink_to('missing.a')
        with self.assertRaises(RuntimeError):
            prepare_build.relocate(source, destination)
        self.assertTrue(destination.is_symlink())
        self.assertEqual(source.read_text(), 'keep')

    def test_package_uses_only_production_inventory(self):
        """Stale bundles, tests and demos cannot leak into a release."""
        self.fixture()
        package.package(self.root)
        package.check(self.root)
        destination = self.root / 'dist/TikuDesktop'
        self.assertEqual([p.name for p in (destination / 'apps').iterdir()],
                         ['editor'])
        self.assertFalse((destination / 'apps/editor/test_unwanted.c').exists())
        self.assertTrue((destination / 'apps/editor/tiku-edit').is_file())
        self.write('dist/TikuDesktop/stale-file.txt')
        package.package(self.root)
        self.assertFalse((destination / 'stale-file.txt').exists())

    def test_package_preserves_unrecognized_destination(self):
        """Packaging will not remove an unmarked user-created directory."""
        self.fixture()
        path = self.write('dist/TikuDesktop/notes.txt', 'keep')
        with self.assertRaises(RuntimeError):
            package.package(self.root)
        self.assertEqual(path.read_text(), 'keep')

    def test_missing_module_preserves_previous_package(self):
        """An incomplete rebuild cannot displace a working distribution."""
        self.fixture()
        package.package(self.root)
        (self.root / 'build/runtime/apps/editor/Editor.so').unlink()
        with self.assertRaises(FileNotFoundError):
            package.package(self.root)
        package.check(self.root)

    def test_nonexecutable_binary_cannot_replace_working_package(self):
        """Validate staged executables before publishing a replacement."""
        self.fixture()
        package.package(self.root)
        (self.root / 'build/runtime/system/TikuDesktop').chmod(0o644)
        with self.assertRaisesRegex(RuntimeError, 'executable'):
            package.package(self.root)
        package.check(self.root)

    def test_package_check_rejects_missing_launcher(self):
        """Checking system binaries alone does not prove the entry point exists."""
        self.fixture()
        package.package(self.root)
        (self.root / 'dist/TikuDesktop/TikuDesktop').unlink()
        with self.assertRaises(RuntimeError):
            package.check(self.root)

    def test_package_check_rejects_missing_resources(self):
        """All runtime assets must accompany the executables, not only the index."""
        self.fixture()
        package.package(self.root)
        (self.root / 'dist/TikuDesktop/assets/strings/en.xml').unlink()
        with self.assertRaises(RuntimeError):
            package.check(self.root)

    def test_package_check_rejects_unlisted_bundle(self):
        """An extra bundle is rejected even if the metadata list is unchanged."""
        self.fixture()
        package.package(self.root)
        self.write('dist/TikuDesktop/apps/unlisted/Unlisted.so')
        with self.assertRaises(RuntimeError):
            package.check(self.root)

    def test_package_check_rejects_duplicate_metadata(self):
        """Package metadata is a unique inventory, not merely a matching set."""
        self.fixture()
        package.package(self.root)
        self.write('dist/TikuDesktop/.tiku-package.json',
                   json.dumps({'version': 1, 'apps': ['editor', 'editor']}))
        with self.assertRaises(RuntimeError):
            package.check(self.root)

    def test_inventory_rejects_empty_duplicates_and_path_escape(self):
        """Manifest inventory must be nonempty, unique and bundle-relative."""
        with self.assertRaises(RuntimeError):
            package.manifests(self.root)
        self.fixture()
        extra = self.write('apps/other/other.app', 'id=editor\nmodule=Other.so\n')
        with self.assertRaises(RuntimeError):
            package.manifests(self.root)
        extra.write_text('id=escape\nmodule=../../Other.so\n')
        with self.assertRaises(RuntimeError):
            package.manifests(self.root)

    def test_failed_publish_restores_previous_package(self):
        """A failure of the final rename restores the last generated package."""
        self.write('destination/.tiku-package.json', '{}')
        self.write('destination/content', 'keep')
        self.write('stage/.tiku-package.json', '{}')
        rename = Path.rename

        def fail_stage(path, target):
            """Inject failure only at the publication seam."""
            if path == self.root / 'stage':
                raise OSError('injected rename failure')
            return rename(path, target)

        with patch.object(Path, 'rename', fail_stage):
            with self.assertRaises(OSError):
                package.replace_generated(self.root / 'stage', self.root / 'destination')
        self.assertEqual((self.root / 'destination/content').read_text(), 'keep')

    def launch(self, executable, **overrides):
        """Launch from an unrelated working directory with controlled overrides."""
        env = {key: value for key, value in os.environ.items()
               if key not in ('TIKU_APPS', 'TIKU_HELP', 'TIKU_STRINGS')}
        env.update(overrides)
        return subprocess.check_output([str(executable), 'two words'],
                                       cwd='/tmp', env=env, text=True).splitlines()

    def test_relocated_launcher_and_overrides(self):
        """A copied runtime finds its own resources, including paths with spaces."""
        self.fixture()
        package.package(self.root)
        target = self.root / 'moved desktop'
        (self.root / 'dist/TikuDesktop').rename(target)
        output = self.launch(target / 'TikuDesktop')
        self.assertEqual(output, [str(target / 'apps'), str(target / 'assets/help'),
                                  str(target / 'assets/strings'), 'two words'])
        output = self.launch(target / 'TikuTracker', TIKU_HELP='/custom/help')
        self.assertEqual(output[1], '/custom/help')

    def test_legacy_launcher_alias(self):
        """The original build/TikuDesktop entry point uses packaged resources."""
        self.fixture()
        prepare_build.prepare(self.root)
        package.resources(self.root / 'build/runtime/assets', self.root)
        package.launchers(self.root / 'build/runtime', self.root)
        output = self.launch(self.root / 'build/TikuDesktop')
        self.assertEqual(output[0], str(self.root / 'build/runtime/apps'))

    def test_mac_layout_and_legacy_bundle_preservation(self):
        """Check Finder layout without claiming to execute native macOS tools."""
        self.fixture()
        package.package(self.root)
        self.write('build/TikuDesktop.app/old', 'keep')
        with patch.object(package.subprocess, 'run') as run:
            package.mac_bundle('cocoa', self.root)
        self.assertEqual(run.call_count, 2)
        bundle = self.root / 'dist/TikuDesktop.app'
        info = plistlib.loads((bundle / 'Contents/Info.plist').read_bytes())
        self.assertEqual(info['CFBundleExecutable'], 'TikuDesktop')
        self.assertTrue((bundle / 'Contents/MacOS/TikuDesktop-bin').is_file())
        self.assertTrue((bundle / 'Contents/Resources/assets/help/index.html').is_file())
        self.assertEqual([p.name for p in bundle.iterdir()], ['Contents'])
        self.assertEqual((self.root / 'build/TikuDesktop.app').resolve(), bundle)
        self.assertEqual((self.root / 'build/TikuDesktop.app.legacy-backup/old').read_text(),
                         'keep')
        output = self.launch(bundle / 'Contents/MacOS/TikuDesktop')
        self.assertEqual(Path(output[1]).resolve(), bundle / 'Contents/Resources/assets/help')

    def test_source_groups_and_inventory_are_not_empty(self):
        """Moved sources and suites remain visible to ownership checks and make."""
        for group in ('system/desktop', 'system/tracker', 'system/shared',
                      'system/deskbar', 'system/runner', 'system/supervisor',
                      'tests/system', 'tests/kits', 'tests/apps', 'tests/integration',
                      'examples/apps', 'examples/diagnostics'):
            self.assertTrue(list((ROOT / group).rglob('*.c')), group)
        inventory = package.manifests(ROOT)
        self.assertTrue({'basic', 'c', 'editor', 'terminal', 'monitor', 'vfs'} <=
                        set(inventory))
        suites = list((ROOT / 'tests').glob('*/test_*.c'))
        self.assertEqual(len(suites), len({path.name for path in suites}))

    def test_fixture_bundle_paths_use_runtime_ids(self):
        """Source-folder renames must not change installed application IDs."""
        inventory = {app_id + '/' + module for app_id, (_, module)
                     in package.manifests(ROOT).items()}
        for manifest in (ROOT / 'examples/apps').rglob('*.app'):
            fields = dict(line.split('=', 1) for line in manifest.read_text().splitlines()
                          if '=' in line and not line.startswith('#'))
            inventory.add(fields['id'] + '/' + fields['module'])
        found = set()
        for source in (ROOT / 'tests/integration/shell/setup').glob('*.sh'):
            found.update(re.findall(r'build/runtime/apps/([^"\s$]+\.so)',
                                    source.read_text()))
        self.assertTrue(found)
        self.assertFalse(found - inventory, found - inventory)


if __name__ == '__main__':
    unittest.main()
