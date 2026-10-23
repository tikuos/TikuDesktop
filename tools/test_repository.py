#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Check pinned framework, standalone lint and board overlay source paths."""
from pathlib import Path
import configparser
import json
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RepositoryTests(unittest.TestCase):
    """Exercise independent checkout contracts without opening a device."""

    def test_stomme_pin(self):
        """The Git submodule and source-content lock select the same framework."""
        config = configparser.ConfigParser()
        config.read(ROOT / '.gitmodules')
        self.assertEqual(config['submodule "foundation"']['url'], 'https://github.com/tikuos/Stomme.git')
        entry = subprocess.check_output(['git', 'ls-files', '--stage', 'foundation'], cwd=ROOT, text=True).split()
        self.assertEqual(entry[0], '160000')
        actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT / 'foundation', text=True).strip()
        self.assertEqual(actual, entry[1])
        pin = subprocess.check_output(['python3', 'foundation/tools/product_bridge.py', '--hash'], cwd=ROOT, text=True).strip()
        lock = json.loads((ROOT / 'foundation.lock.json').read_text())
        self.assertEqual(pin, lock['source_sha256'])
        self.assertEqual(lock['remote'], config['submodule "foundation"']['url'])

    def test_board_overlay_sources(self):
        """Enabled board applications resolve their kit sources through Stomme."""
        with tempfile.TemporaryDirectory(prefix='desktop-overlay-') as temp:
            work = Path(temp)
            (work / 'applications').symlink_to(ROOT, target_is_directory=True)
            rules = 'include applications/applications.mk\nprint:\n\t@echo $(APPL_SRCS)\n'
            command = ['make', '-s', '-f', '-', 'print', 'TIKU_APPL_GUI=1',
                       'TIKU_APPL_DOC=1', 'TIKU_APPL_GUI_DEMO=1',
                       'TIKU_APPL_DRAW_DEMO=1', 'TIKU_APPL_DOC_DEMO=1']
            result = subprocess.run(command, cwd=work, input=rules, text=True,
                                    capture_output=True, check=True)
            missing = [name for name in result.stdout.split() if not (work / name).is_file()]
            self.assertEqual(missing, [])
            disabled = subprocess.run(['make', '-s', '-f', '-', 'print'], cwd=work,
                                      input=rules, text=True, capture_output=True, check=True)
            self.assertEqual(disabled.stdout.strip(), '')

    def test_lint_without_firmware(self):
        """Comment checks remain enabled with no firmware checkout."""
        with tempfile.TemporaryDirectory(prefix='desktop-lint-') as temp:
            result = subprocess.run(['make', '-n', 'lint', 'BUILD=' + temp + '/build',
                                     'TIKUOS=' + temp + '/absent'], cwd=ROOT,
                                    capture_output=True, text=True, check=True)
            self.assertIn('foundation/tools/check_comment_style.py', result.stdout)
            self.assertNotIn('UNCHECKED', result.stdout)
            self.assertNotIn('SKIPPED', result.stdout)


    def test_product_hook_installation(self):
        """The product installer applies Stomme's rules to the product checkout."""
        with tempfile.TemporaryDirectory(prefix='desktop-hooks-') as temp:
            work = Path(temp)
            environment = {key: value for key, value in os.environ.items()
                           if not key.startswith('GIT_') and key != 'EMAIL'}
            subprocess.run(['git', 'init', '-q', str(work)], check=True, env=environment)
            for key, value in [('user.name', 'Fixture Author'), ('user.email', 'fixture@example.test')]:
                subprocess.run(['git', 'config', key, value], cwd=work, check=True, env=environment)
            (work / 'foundation').symlink_to(ROOT / 'foundation', target_is_directory=True)
            (work / 'hygiene').mkdir()
            shutil.copy2(ROOT / 'hygiene/install_hooks.sh', work / 'hygiene/install_hooks.sh')
            subprocess.run(['sh', 'hygiene/install_hooks.sh'], cwd=work, env=environment,
                           check=True, capture_output=True)
            path = subprocess.check_output(['git', 'config', '--local', 'core.hooksPath'],
                                           cwd=work, env=environment, text=True).strip()
            self.assertEqual(path, 'foundation/hygiene/githooks')
            message = work / 'message'
            for text, code in [('Build: Pinned Stomme dependency', 0), ('invalid subject', 1)]:
                message.write_text(text + '\n')
                result = subprocess.run(['git', 'hook', 'run', 'commit-msg', '--', str(message)],
                                        cwd=work, env=environment, capture_output=True)
                self.assertEqual(result.returncode, code, result.stderr.decode())


if __name__ == '__main__':
    unittest.main()
