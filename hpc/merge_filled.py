#!/usr/bin/env python3
"""Join per-chromosome genotypes.filled files into one whole-genome file.

Usage:
  python3 hpc/merge_filled.py RUN_ROOT OUTPUT_FILE [--file genotypes.filled]

RUN_ROOT holds one folder per chromosome, named chr1/, chr2/, ... (as
written by hpc/split_by_chromosome.py) or just 1/, 2/, ... 30/.
Chromosomes are joined in numeric order, which is the marker order of the
original chromosome.data.

Works for any file in the fixed FindHap genotype layout (animal, chip,
count, genotypes): genotypes.filled (default) or, with
--file genotypes.txt, the original genotypes.

Every chromosome must list the same animals in the same order (true with
damout = 0). If not, the script stops and names the first animal that
differs, instead of writing a file with misaligned columns.
"""
import argparse
import os
import re
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('root')
    parser.add_argument('output')
    parser.add_argument('--file', default='genotypes.filled', help='file to join (default: genotypes.filled)')
    args = parser.parse_args()

    folders = sorted((int(m.group(1)), name) for name in os.listdir(args.root)
                     if (m := re.fullmatch(r'(?:chr)?(\d+)', name)) and os.path.isdir(os.path.join(args.root, name)))
    if not folders:
        sys.exit(f"no chromosome folders (chr<N> or <N>) in {args.root}")
    paths = [os.path.join(args.root, name, args.file) for _, name in folders]
    for path in paths:
        if not os.path.exists(path):
            sys.exit(f"missing {path}")
    inputs = [open(p, buffering=1 << 22) for p in paths]
    animals = 0
    loci_total = None
    with open(args.output, 'w', buffering=1 << 22) as out:
        while True:
            lines = [f.readline() for f in inputs]
            ended = [not line for line in lines]
            if all(ended):
                break
            if any(ended):
                short = [folders[i][1] for i, e in enumerate(ended) if e]
                sys.exit(f"{', '.join(short)}: fewer animals than the other chromosomes "
                         f"(after {animals} animals); not merged")
            ids = {line[0:10] for line in lines}
            if len(ids) != 1:
                detail = ', '.join(f"{folders[i][1]}={line[0:10].strip()}" for i, line in enumerate(lines))
                sys.exit(f"animal lists differ at record {animals + 1}: {detail}; not merged")
            parts = [line[31:].rstrip('\n') for line in lines]
            counts = [int(line[20:30]) for line in lines]
            for i, (part, count) in enumerate(zip(parts, counts)):
                if len(part) != count:
                    sys.exit(f"{paths[i]}: record {animals + 1} has {len(part)} genotypes, header says {count}")
            total = sum(counts)
            loci_total = max(loci_total or 0, total)
            out.write(f"{lines[0][0:20]}{total:10d} {''.join(parts)}\n")
            animals += 1
    for f in inputs:
        f.close()
    print(f"{animals} animals, up to {loci_total} loci per animal from {len(folders)} chromosomes "
          f"({folders[0][1]}..{folders[-1][1]}) -> {args.output}")


if __name__ == '__main__':
    main()
