#!/usr/bin/env python3
"""Compare the imputation results of two FindHap/TurboHap runs.

Usage:
  python3 tools/compare_outputs.py DIR_A DIR_B [--names A B]

Each folder holds the outputs of one run on the same inputs (at least
genotypes.filled; hap.filled is used too when present). Prints:
  - animals present in only one run;
  - genotype codes in each run (0, 1, 2 = called; 3, 4 = partial; 5 = missing);
  - a table of how each code in A became in B (where B imputes less, the
    counts move from 0/1/2 into 5);
  - the imputed-SNP totals from hap.filled and the animals with the
    largest drop.
Works line by line, so it also handles very large files.
"""
import argparse
import os
import sys

CODES = '012345'


def records(path):
    with open(path, 'rb') as f:
        for line in f:
            yield int(line[0:10]), line[31:].rstrip(b'\r\n')


def hap_filled(path):
    out = {}
    if not os.path.exists(path):
        return out
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) >= 6 and p[0].isdigit():
                out[int(p[0])] = (int(p[3]), int(p[4]), float(p[5]))   # observed, filled, corr2
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('a'); ap.add_argument('b')
    ap.add_argument('--names', nargs=2, default=['A', 'B'])
    args = ap.parse_args()
    na, nb = args.names
    fa, fb = (os.path.join(d, 'genotypes.filled') for d in (args.a, args.b))
    for f in (fa, fb):
        if not os.path.exists(f):
            sys.exit(f'missing {f}')

    # Index B by animal (offset of each record) to allow different orders.
    index = {}
    with open(fb, 'rb') as f:
        pos = 0
        for line in f:
            index[int(line[0:10])] = pos
            pos += len(line)
    trans = [[0] * 6 for _ in range(6)]
    other = 0
    count_a = [0] * 6; count_b = [0] * 6
    seen = set(); only_a = []; length_mismatch = 0
    per_animal_drop = []
    with open(fb, 'rb') as fbh:
        for animal, ga in records(fa):
            if animal not in index:
                only_a.append(animal); continue
            seen.add(animal)
            fbh.seek(index[animal]); gb = fbh.readline()[31:].rstrip(b'\r\n')
            if len(ga) != len(gb):
                length_mismatch += 1; continue
            if ga == gb:
                for c in range(6):
                    k = ga.count(CODES[c].encode()); count_a[c] += k; count_b[c] += k; trans[c][c] += k
                continue
            called_a = called_b = 0
            for x, y in zip(ga, gb):
                i, j = x - 48, y - 48
                if 0 <= i < 6 and 0 <= j < 6:
                    trans[i][j] += 1; count_a[i] += 1; count_b[j] += 1
                    called_a += i <= 2; called_b += j <= 2
                else:
                    other += 1
            if called_b < called_a:
                per_animal_drop.append((called_a - called_b, animal))
    only_b = [a for a in index if a not in seen]

    total = sum(count_a)
    print(f'animals: {len(seen)} in both, {len(only_a)} only in {na}, {len(only_b)} only in {nb}'
          + (f', {length_mismatch} with different SNP counts (skipped)' if length_mismatch else ''))
    for label, ids in ((na, only_a), (nb, only_b)):
        if ids:
            print(f'  only in {label} (first 10): {" ".join(map(str, ids[:10]))}')
    if total == 0:
        sys.exit('no common animals')
    print(f'\ngenotype codes in common animals ({total} genotypes):')
    print('            ' + ''.join(f'{c:>12s}' for c in ('0', '1', '2', '3', '4', '5')) + '    called 0-2')
    for label, cnt in ((na, count_a), (nb, count_b)):
        called = sum(cnt[:3])
        print(f'  {label:10s}' + ''.join(f'{x:12d}' for x in cnt) + f'   {100 * called / total:8.4f}%')
    diff = total - sum(trans[i][i] for i in range(6))
    print(f'\ngenotypes that differ: {diff} ({100 * diff / total:.4f}%)')
    if diff:
        print(f'  rows = code in {na}, columns = code in {nb} (off-diagonal only)')
        print('       ' + ''.join(f'{c:>11s}' for c in CODES))
        for i in range(6):
            row = ''.join(f'{(trans[i][j] if i != j else 0):11d}' for j in range(6))
            print(f'    {CODES[i]}  {row}')
        less = sum(trans[i][j] for i in range(3) for j in range(3, 6))
        more = sum(trans[i][j] for i in range(3, 6) for j in range(3))
        swap = sum(trans[i][j] for i in range(3) for j in range(3) if i != j)
        print(f'  called in {na}, not called in {nb}: {less}')
        print(f'  called in {nb}, not called in {na}: {more}')
        print(f'  called in both but different: {swap}')
    if other:
        print(f'  ({other} characters outside 0-5 ignored)')

    ha, hb = hap_filled(os.path.join(args.a, 'hap.filled')), hap_filled(os.path.join(args.b, 'hap.filled'))
    if ha and hb:
        common = [a for a in ha if a in hb]
        sa = sum(ha[a][1] for a in common); sb = sum(hb[a][1] for a in common)
        lower = sum(1 for a in common if hb[a][1] < ha[a][1])
        higher = sum(1 for a in common if hb[a][1] > ha[a][1])
        print(f'\nhap.filled, {len(common)} common animals: filled SNPs total {na}={sa} {nb}={sb} '
              f'({100 * (sb - sa) / max(sa, 1):+.4f}%); {nb} lower for {lower} animals, higher for {higher}')
    if per_animal_drop:
        per_animal_drop.sort(reverse=True)
        print(f'\nanimals with fewer called genotypes in {nb}: {len(per_animal_drop)}; largest drops:')
        for drop, animal in per_animal_drop[:10]:
            extra = ''
            if animal in ha and animal in hb:
                extra = (f'  (observed {ha[animal][0]}, filled {na} {ha[animal][1]} / {nb} {hb[animal][1]},'
                         f' corr2 {ha[animal][2]:.4f} / {hb[animal][2]:.4f})')
            print(f'  {animal:>10d}  -{drop}{extra}')


if __name__ == '__main__':
    main()
