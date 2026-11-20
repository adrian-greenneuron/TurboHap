#!/usr/bin/env python3
"""Split a FindHap/TurboHap input set into one folder per chromosome.

Usage:
  python3 hpc/split_by_chromosome.py INPUT_DIR OUTPUT_ROOT

INPUT_DIR holds findhap.options, pedigree.file, chromosome.data and
genotypes.txt for the whole genome. For each chromosome N this writes
OUTPUT_ROOT/chrN/ with:
  chromosome.data  the markers of N, with the overall index and every
                   chip's marker positions renumbered within N;
  genotypes.txt    for every animal, only the genotype characters of N
                   (in chip order), with the locus count updated;
  pedigree.file, findhap.options   symbolic links to the originals.

The same findhap.options works for every chromosome: Xchrom keeps the X
chromosome's original number and has no effect on the other folders.
Animals and their order are unchanged, so outputs can be joined back with
hpc/merge_filled.py.
"""
import operator
import os
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, root = sys.argv[1], sys.argv[2]
    with open(os.path.join(src, 'chromosome.data')) as f:
        header = f.readline()
        rows = [line.split() for line in f if line.strip()]
    nchips = int(rows[0][5])
    markers = [(r[0], int(r[1]), int(r[2]), int(r[4]), [int(x) for x in r[6:6 + nchips]]) for r in rows]
    chroms = sorted({m[1] for m in markers})

    outputs = {}
    for c in chroms:
        ms = [m for m in markers if m[1] == c]
        kept = {chip: [] for chip in range(1, nchips + 1)}  # original chip positions kept, in chip order
        counters = [0] * nchips
        folder = os.path.join(root, f'chr{c}')
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, 'chromosome.data'), 'w') as f:
            f.write(header)
            for overall, (name, chrom, within, location, chip_pos) in enumerate(ms, 1):
                new = []
                for chip in range(nchips):
                    if chip_pos[chip] > 0:
                        counters[chip] += 1
                        kept[chip + 1].append(chip_pos[chip])
                        new.append(counters[chip])
                    else:
                        new.append(0)
                f.write(f"{name} {chrom:4d} {within:8d} {overall:8d} {location:10d} {nchips:4d} "
                        + " ".join(f"{p:8d}" for p in new) + "\n")
        for name in ('pedigree.file', 'findhap.options'):
            link = os.path.join(folder, name)
            if not os.path.lexists(link):
                os.symlink(os.path.abspath(os.path.join(src, name)), link)
        pickers = {}
        for chip, positions in kept.items():
            positions.sort()
            if len(positions) > 1:
                pickers[chip] = (operator.itemgetter(*[p - 1 for p in positions]), len(positions))
            elif positions:
                p = positions[0] - 1
                pickers[chip] = ((lambda g, p=p: (g[p],)), 1)
            else:
                pickers[chip] = ((lambda g: ()), 0)
        outputs[c] = (pickers, open(os.path.join(folder, 'genotypes.txt'), 'w', buffering=1 << 22))

    animals = 0
    with open(os.path.join(src, 'genotypes.txt')) as f:
        for line in f:
            animal, chip, n = line[0:10], int(line[10:20]), int(line[20:30])
            g = line[31:31 + n]
            if len(g) != n:
                sys.exit(f"genotypes.txt: animal {animal.strip()} has {len(g)} genotypes, expected {n}")
            for pickers, out in outputs.values():
                if chip not in pickers:
                    sys.exit(f"genotypes.txt: animal {animal.strip()} uses chip {chip}, not in chromosome.data")
                pick, count = pickers[chip]
                out.write(f"{animal}{chip:10d}{count:10d} {''.join(pick(g))}\n")
            animals += 1
    for _, out in outputs.values():
        out.close()
    print(f"{animals} animals, {len(markers)} markers split into chromosomes {chroms[0]}..{chroms[-1]} "
          f"({len(chroms)} folders) under {root}")


if __name__ == '__main__':
    main()
