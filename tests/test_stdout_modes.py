#!/usr/bin/env python3
"""Console modes must preserve all ten reference files and useful diagnostics."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STAMP = re.compile(r'^\[\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z \+\d+\.\d{3}s\] ')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--default', choices=('legacy', 'modern'), required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    example = ROOT/'examples/small'
    reference = json.loads((example/'expected.json').read_text())
    env = dict(os.environ, LC_ALL='C', OMP_NUM_THREADS='2', OMP_DYNAMIC='FALSE')
    with tempfile.TemporaryDirectory(prefix='turbohap-stdout-') as temporary:
        base = Path(temporary)
        for engine, engine_flags in (('standard', []), ('LM', ['--low-memory'])):
            for selection in (None, 'legacy', 'modern'):
                mode = selection or args.default
                work = base/f'{engine}-{selection or "default"}'
                work.mkdir()
                for name in reference['inputs']:
                    shutil.copyfile(example/name, work/name)
                flags = [f'--stdout={selection}'] if selection else []
                result = subprocess.run([str(binary), '2', *engine_flags, *flags], cwd=work,
                                        env=env, text=True, capture_output=True, timeout=45)
                require(result.returncode == 0, f'{engine}/{selection}: {result.stderr}')
                for name, expected in reference['outputs'].items():
                    data = (work/name).read_bytes()
                    require(len(data) == expected['bytes'] and
                            hashlib.sha256(data).hexdigest() == expected['sha256'],
                            f'{engine}/{selection}: changed bytes in {name}')
                require(set(p.name for p in work.iterdir()) ==
                        set(reference['inputs']) | set(reference['outputs']),
                        'Console selection created an unexpected file')
                if mode == 'modern':
                    require(all(STAMP.match(line) for line in result.stdout.splitlines()),
                            'Modern record is missing its timestamp')
                    for phrase in ('Population matching started', 'Population matching completed',
                                   'Pedigree matching started', 'Pedigree matching completed',
                                   'Result output started', 'Result output completed',
                                   'before pruning', 'run peak', 'headroom', 'completed normally'):
                        require(phrase in result.stdout, f'Missing modern message: {phrase}')
                    require('First 10 genotypes created' not in result.stdout,
                            'Modern mode retained the legacy preview')
                else:
                    require('[20' not in result.stdout and 'TurboHap capacity:' not in result.stdout,
                            'Legacy mode contains modern progress records')
                    require('First 10 genotypes created' in result.stdout and
                            'Program findhap completed normally' in result.stdout,
                            'Legacy summaries missing')
            # Capacity failure reports a proven lower bound, never a retained maximum.
            work = base/f'{engine}-capacity-failure'
            work.mkdir()
            for name in reference['inputs']:
                shutil.copyfile(example/name, work/name)
            options = (work/'findhap.options').read_text().splitlines()
            fields = options[0].split()
            fields[5] = '1'
            options[0] = ' '.join(fields)
            (work/'findhap.options').write_text('\n'.join(options)+'\n')
            result = subprocess.run([str(binary), '2', *engine_flags, '--stdout=modern'],
                                    cwd=work, env=env, text=True, capture_output=True, timeout=45)
            require(result.returncode == 100, 'Unexpected capacity-failure exit status')
            require('requires at least 2 slots; maxhap 1' in result.stdout and
                    'Increase maxhap' in result.stdout and
                    'completed normally' not in result.stdout,
                    'Capacity failure diagnostic is incomplete')
    print('PASS: both engines, three console selections, 60 raw reference checks and two capacity failures')


if __name__ == '__main__':
    main()
