#!/usr/bin/env python3
"""Check both modes against saved original-Fortran bytes on the bundled example."""
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
    data = path.read_bytes()
    if len(data) != expected['bytes'] or hashlib.sha256(data).hexdigest() != expected['sha256']:
        raise RuntimeError(f'Byte comparison failed: {path.name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    example = ROOT / 'examples/small'
    expected = json.loads((example / 'expected.json').read_text())
    for name, record in expected['inputs'].items():
        check_file(example / name, record)
    with tempfile.TemporaryDirectory(prefix='turbohap-example-') as temporary:
        for mode, flags in [('TurboHap', []), ('TurboHap LM', ['--low-memory'])]:
            for threads in (1, 4):
                run = Path(temporary) / f'{mode}-{threads}'
                run.mkdir()
                for name in expected['inputs']:
                    shutil.copyfile(example / name, run / name)
                env = dict(os.environ, LC_ALL='C', OMP_NUM_THREADS=str(threads), OMP_DYNAMIC='FALSE')
                result = subprocess.run([str(binary), str(threads), *flags], cwd=run,
                                        env=env, capture_output=True)
                if result.returncode or b'Program findhap completed normally' not in result.stdout:
                    raise RuntimeError(f'{mode} failed: {result.stderr.decode(errors="replace")}')
                outputs = {p.name for p in run.iterdir()} - set(expected['inputs'])
                if outputs != set(expected['outputs']):
                    raise RuntimeError(f'{mode}: unexpected output file set: {outputs}')
                for name, record in expected['outputs'].items():
                    check_file(run / name, record)
                print(f'PASS {mode}, {threads} thread(s): every output byte matches')


if __name__ == '__main__':
    main()
