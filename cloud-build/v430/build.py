#!/usr/bin/env python3
"""Pinned Linux x86_64 install and compile; never uploads or edits firmware."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from datetime import datetime, timezone

HERE = Path(__file__).resolve().parent
LOCK = json.loads((HERE / 'versions.lock.json').read_text())


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def download(item):
    with urllib.request.urlopen(item['url'], timeout=300) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != item['sha256']:
        raise RuntimeError('Download SHA-256 mismatch')
    return data


def run(root, label, command, env=None):
    print(label, flush=True)
    with (root / 'logs' / (label + '.log')).open('w') as output:
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, env=env)
    if result.returncode:
        raise RuntimeError(f'{label} failed ({result.returncode}); see logs/{label}.log')


def cli(root):
    return [str(root / 'bin/arduino-cli'), '--config-file', str(root / 'arduino-cli.yaml')]


def install(root):
    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        raise RuntimeError('This lock targets Linux x86_64')
    if platform.python_version() != LOCK['python']:
        raise RuntimeError('Use Python ' + LOCK['python'] + ' to match the validated environment')
    # Require an empty location so previous packages/caches cannot affect resolution.
    if root.exists() and any(root.iterdir()):
        raise RuntimeError('Install directory must be empty; use a fresh directory')
    for name in ['bin', 'data', 'downloads', 'user', 'logs']:
        (root / name).mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(download(LOCK['cli_archive'])), mode='r:gz') as archive:
        (root / 'bin/arduino-cli').write_bytes(archive.extractfile('arduino-cli').read())
    (root / 'bin/arduino-cli').chmod(0o755)
    for filename in ['package_index.json', 'package_esp32_index.json', 'library_index.json']:
        shutil.copyfile(HERE / filename, root / 'data' / filename)
    config = {'directories': {k: str(root / v) for k, v in
                             [('data', 'data'), ('downloads', 'downloads'), ('user', 'user')]},
              'build_cache': {'path': str(root / 'build-cache')},
              'board_manager': {'additional_urls': ['https://espressif.github.io/arduino-esp32/package_esp32_index.json']}}
    proxy = os.environ.get('HTTPS_PROXY') or os.environ.get('https_proxy') or os.environ.get('HTTP_PROXY')
    if proxy:
        config['network'] = {'proxy': proxy}
    (root / 'arduino-cli.yaml').write_text(json.dumps(config, indent=2) + '\n')
    (root / 'arduino-cli.yaml').chmod(0o600)
    run(root, 'python-venv', [sys.executable, '-m', 'venv', str(root / 'python-env')])
    wheel = root / 'downloads' / LOCK['pyserial_wheel']['filename']
    wheel.write_bytes(download(LOCK['pyserial_wheel']))
    run(root, 'python-dependencies', [str(root / 'python-env/bin/python'), '-m', 'pip',
                                    'install', '--no-index', '--no-deps', str(wheel)])
    run(root, 'cli-version', cli(root) + ['version'])
    run(root, 'core-install', cli(root) + ['core', 'install', LOCK['core']])
    # Do not resolve floating transitive dependencies. Every installed library is explicit.
    run(root, 'library-install', cli(root) + ['lib', 'install', '--no-deps'] +
        [name + '@' + version for name, version in LOCK['libraries'].items()])
    actual = {}
    for path in (root / 'user/libraries').glob('*/library.properties'):
        props = dict(line.split('=', 1) for line in path.read_text().splitlines()
                     if '=' in line and not line.startswith('#'))
        actual[props['name']] = props['version']
    if actual != LOCK['libraries']:
        raise RuntimeError('Installed libraries differ from lock')
    (root / 'installed-libraries.json').write_text(json.dumps(actual, indent=2) + '\n')
    run(root, 'core-list', cli(root) + ['core', 'list', '--json'])
    run(root, 'library-list', cli(root) + ['lib', 'list', '--json'])


def compile_sketch(root, source):
    def check_source():
        for path, expected in LOCK['source_sha256'].items():
            if digest(source / path) != expected:
                raise RuntimeError('Source differs from safe baseline: ' + path)
    check_source()
    # A compile always gets a new build/cache directory; refuse accidental reuse.
    for name in ['build', 'build-cache', 'artifacts']:
        if (root / name).exists():
            raise RuntimeError('Compile output exists; use a fresh environment')
    command = cli(root) + ['compile', '--fqbn', LOCK['fqbn'], '--warnings', 'all',
                          '--verbose', '--clean', '--jobs', '2', '--build-path', str(root / 'build'),
                          '--output-dir', str(root / 'artifacts'),
                          str(source / 'firmware/V430_RTC_NTP_FINAL')]
    (root / 'compile-command.json').write_text(json.dumps(command, indent=2) + '\n')
    env = os.environ.copy()
    env['PATH'] = str(root / 'python-env/bin') + os.pathsep + env['PATH']
    started = datetime.now(timezone.utc).isoformat()
    print('first-compile', flush=True)
    with (root / 'logs/first-compile.log').open('w') as output:
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, env=env)
    check_source()
    log = (root / 'logs/first-compile.log').read_text()
    report = {'started_utc': started, 'finished_utc': datetime.now(timezone.utc).isoformat(),
              'exit_code': result.returncode, 'fqbn': LOCK['fqbn'], 'source_unchanged': True,
              'size_output': [line for line in log.splitlines() if line.startswith(('Sketch uses', 'Global variables use'))],
              'warnings': [line for line in log.splitlines() if 'warning:' in line]}
    (root / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    if result.returncode:
        raise RuntimeError('Compile failed; complete output retained in logs/first-compile.log')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['install', 'compile'])
    parser.add_argument('--env-dir', required=True, type=Path)
    parser.add_argument('--source-dir', type=Path, default=HERE.parents[1])
    args = parser.parse_args()
    root = args.env_dir.resolve()
    if args.action == 'install':
        install(root)
    else:
        compile_sketch(root, args.source_dir.resolve())


if __name__ == '__main__':
    main()
