"""Exhaustive validation reads inputs only and catches unsampled interior errors."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    example = Path(__file__).resolve().parents[1] / 'examples/small'
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        for name in ('findhap.options', 'pedigree.file', 'chromosome.data', 'genotypes.txt'):
            shutil.copy2(example / name, root / name)
        sentinel = root / 'genotypes.filled'
        sentinel.write_bytes(b'existing results must remain unchanged\n')
        original = {p.name: p.read_bytes() for p in root.iterdir()}
        def invoke():
            return subprocess.run([str(binary), '--directory', str(root), '--json'], capture_output=True, text=True)
        result = invoke()
        if result.returncode or json.loads(result.stdout)['error_count']:
            raise ValueError(f'Valid fixture failed: {result.stderr}')
        if {p.name: p.read_bytes() for p in root.iterdir()} != original:
            raise ValueError('Standalone checker changed files')
        row = (root / 'genotypes.txt').read_bytes().splitlines()[0]
        for position in (1, 50, 1100):
            rows = [row] * 1100
            damaged = bytearray(rows[position - 1])
            damaged[40] = ord(' ')
            rows[position - 1] = bytes(damaged)
            (root / 'genotypes.txt').write_bytes(b'\n'.join(rows) + b'\n')
            result = invoke()
            issues = json.loads(result.stdout)['issues']
            if result.returncode != 2 or not any(e['line'] == position for item in issues
                    if item['code'] == 'blank_genotype_fields' for e in item['examples']):
                raise ValueError('Exhaustive validator missed a localized blank')
            if sentinel.read_bytes() != original['genotypes.filled']:
                raise ValueError('Existing output overwritten after failure')
        print('PASS: standalone exhaustive validation, localized errors, no result-file writes')


if __name__ == '__main__':
    main()
