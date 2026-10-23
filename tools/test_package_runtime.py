#!/usr/bin/env python3
# Tiku Operating System v0.06
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# Exercise the shipping desktop and Reader from a relocated distribution.
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import os
import argparse
import shutil
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main(build=ROOT / 'build'):
    """Open packaged Help through the real production application process."""
    with tempfile.TemporaryDirectory(prefix='tiku package ') as temp:
        directory = Path(temp)
        runtime = directory / 'Relocated Desktop'
        shutil.copytree(ROOT / 'dist/TikuDesktop', runtime)
        for language, script in (('en', 'package.script'), ('fr', 'package-fr.script')):
            fixture = directory / language
            (fixture / 'Desktop').mkdir(parents=True)
            env = {key: value for key, value in os.environ.items()
                   if not key.startswith('TIKU_')}
            env.update(HOME=str(fixture), DISPLAY='', TIKU_LANG=language,
                       XDG_CONFIG_HOME=str(fixture / '.config'),
                       XDG_DATA_HOME=str(fixture / '.local/share'))
            command = [str(build / 'dev/trk-conduct'), '-n',
                       'packaged desktop (' + language + ')',
                       str(ROOT / 'tests/integration' / script), '--',
                       str(runtime / 'TikuDesktop')]
            process = subprocess.Popen(command, cwd=directory, env=env,
                                       start_new_session=True)
            try:
                result = process.wait(timeout=45)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                raise RuntimeError('Packaged desktop smoke test timed out') from None
            if result != 0:
                raise RuntimeError('Packaged desktop smoke test failed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    main(parser.parse_args().build.resolve())
