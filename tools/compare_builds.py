#!/usr/bin/env python3
"""Compare two TurboHap/FindHap builds on the same inputs.

Usage:
  python3 tools/compare_builds.py ORIGINAL_BINARY MODIFIED_BINARY INPUT_DIR \
      [--threads 8,16,32] [--low-memory] [--keep]

INPUT_DIR holds findhap.options, pedigree.file, chromosome.data and
genotypes.txt. Each build runs in its own temporary directory next to
INPUT_DIR (inputs are symlinked, not copied). For every thread count the
script prints wall time and peak memory of both builds and checks that
every output file is byte-for-byte identical.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import time

INPUTS = ['findhap.options', 'pedigree.file', 'chromosome.data', 'genotypes.txt']


def run(binary, inputs, threads, low_memory, parent):
    work = tempfile.mkdtemp(prefix='turbohap-compare-', dir=parent)
    for name in INPUTS:
        os.symlink(os.path.abspath(os.path.join(inputs, name)), os.path.join(work, name))
    env = dict(os.environ, LC_ALL='C', OMP_DYNAMIC='FALSE', OMP_NUM_THREADS=str(threads))
    args = [binary, str(threads)] + (['--low-memory'] if low_memory else [])
    start = time.monotonic()
    with open(os.path.join(work, 'stdout.log'), 'wb') as log:
        process = subprocess.Popen(args, cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT)
        # wait4 gives this child's own peak memory (Linux reports KiB).
        _, status, usage = os.wait4(process.pid, 0)
        process.returncode = os.waitstatus_to_exitcode(status)
        code = process.returncode
    wall = time.monotonic() - start
    peak = usage.ru_maxrss / 1024 / 1024
    hashes = {}
    for name in sorted(os.listdir(work)):
        if name in INPUTS or name == 'stdout.log':
            continue
        digest = hashlib.sha256()
        with open(os.path.join(work, name), 'rb') as handle:
            for block in iter(lambda: handle.read(1 << 20), b''):
                digest.update(block)
        hashes[name] = digest.hexdigest()
    return work, code, wall, peak, hashes


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('original')
    parser.add_argument('modified')
    parser.add_argument('inputs')
    parser.add_argument('--threads', default=str(os.cpu_count() or 1))
    parser.add_argument('--low-memory', action='store_true')
    parser.add_argument('--keep', action='store_true', help='keep the run directories')
    args = parser.parse_args()
    parent = os.path.dirname(os.path.abspath(args.inputs.rstrip('/')))
    all_same = True
    for threads in [int(t) for t in args.threads.split(',')]:
        results = {}
        for label, binary in (('original', args.original), ('modified', args.modified)):
            results[label] = run(os.path.abspath(binary), args.inputs, threads, args.low_memory, parent)
            work, code, wall, peak, _ = results[label]
            peak_text = f'{peak:.2f} GiB'
            status = 'ok' if code == 0 else f'exit {code} (see {work}/stdout.log)'
            print(f'{threads:4d} threads  {label:9s} {wall:9.1f} s  peak {peak_text:>10s}  {status}', flush=True)
        a, b = results['original'][4], results['modified'][4]
        differing = sorted(name for name in set(a) | set(b) if a.get(name) != b.get(name))
        if differing:
            all_same = False
            print(f'           outputs DIFFER: {", ".join(differing)}')
        else:
            speedup = results['original'][2] / max(results['modified'][2], 1e-9)
            print(f'           outputs identical ({len(a)} files), speedup {speedup:.2f}x')
        if not args.keep:
            for result in results.values():
                shutil.rmtree(result[0], ignore_errors=True)
    sys.exit(0 if all_same else 1)


if __name__ == '__main__':
    main()
