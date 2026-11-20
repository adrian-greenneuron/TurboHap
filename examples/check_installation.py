#!/usr/bin/env python3
"""Check a TurboHap installation against the original FindHap outputs for the bundled example."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def check_file(path, expected):
    if path.stat().st_size != expected['bytes']:
        raise RuntimeError(f'Unexpected file size: {path.name}')
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(65536), b''):
            digest.update(chunk)
    if digest.hexdigest() != expected['sha256']:
        raise RuntimeError(f'Unexpected SHA-256: {path.name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/findhap',
                        help='TurboHap executable (default: build/findhap in the source tree)')
    parser.add_argument('--row-schedule', choices=('auto', 'static', 'dynamic'))
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error(f'Executable not found or not executable: {binary}. Use --binary to give its path.')
    example = Path(__file__).resolve().parent / 'small'
    expected = json.loads((example / 'expected.json').read_text())
    for name, record in expected['inputs'].items():
        check_file(example / name, record)
    with tempfile.TemporaryDirectory(prefix='turbohap-example-') as temporary:
        for mode, flags in [('TurboHap', []), ('TurboHap LM', ['--low-memory'])]:
            for threads in (1, 2, 4):
                run = Path(temporary) / f'{mode}-{threads}'
                run.mkdir()
                for name in expected['inputs']:
                    shutil.copyfile(example / name, run / name)
                env = dict(os.environ, LC_ALL='C', OMP_NUM_THREADS=str(threads), OMP_DYNAMIC='FALSE')
                row_flags = ['--row-schedule=' + args.row_schedule] if args.row_schedule else []
                result = subprocess.run([str(binary), str(threads), *flags, *row_flags], cwd=run,
                                        env=env, capture_output=True)
                if result.returncode or b'Program findhap completed normally' not in result.stdout:
                    raise RuntimeError(f'{mode} failed: {result.stderr.decode(errors="replace")}')
                outputs = {p.name for p in run.iterdir()} - set(expected['inputs'])
                if outputs != set(expected['outputs']):
                    raise RuntimeError(f'{mode}: unexpected output file set: {outputs}')
                for name, record in expected['outputs'].items():
                    check_file(run / name, record)
                print(f'PASS {mode}, {threads} thread(s): every output byte matches')
    print('Installation check passed: all 60 output checks matched the original FindHap example.')


if __name__ == '__main__':
    main()
