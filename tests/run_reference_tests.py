"""Run retained differential suites with an explicit, independently pinned oracle."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=('pedigree', 'deep_pedigree'), required=True)
    parser.add_argument('--reference', default='')
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--record', type=Path, required=True)
    args = parser.parse_args()
    if not args.reference:
        print('SKIP: set FINDHAP_REFERENCE_BINARY to an independently qualified executable')
        return 77
    reference = Path(args.reference).resolve(strict=True)
    candidate = args.candidate.resolve(strict=True)
    with reference.open('rb') as handle:
        reference_sha = hashlib.file_digest(handle, 'sha256').hexdigest()
    tests = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='turbohap-reference-') as directory:
        command = [sys.executable]
        if args.suite == 'pedigree':
            command += [str(tests / 'test_pedigree_differential.py'),
                        '--reference', str(reference), '--reference-single-thread']
        else:
            command += [str(tests / 'test_deep_pedigree.py'),
                        '--control', str(reference), '--work', str(Path(directory) / 'cases')]
        command += ['--candidate', str(candidate), '--record', str(args.record), '--threads', '1', '4', '10']
        result = subprocess.run(command)
        if result.returncode:
            return result.returncode
    args.record.with_suffix('.reference.json').write_text(json.dumps({
        'reference_sha256': reference_sha,
        'candidate_sha256': hashlib.sha256(candidate.read_bytes()).hexdigest(),
        'comparison': 'raw byte count and SHA256; no normalization',
    }, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
