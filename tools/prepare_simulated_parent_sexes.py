#!/usr/bin/env python3
"""Prepare pedsim sex labels before generating corrected synthetic truth."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def identity(path):
    return dict(size=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def prepare_parent_sexes(directory):
    """Correct pedsim role/sex conflicts before simulation, never in existing truth."""
    directory = Path(directory)
    for name in ('genotypes.raw', 'genotypes.txt', 'genotypes.true', 'genotypes.true.gz',
                 'haplotype.data', 'haplotype.data.gz'):
        if (directory / name).exists():
            raise ValueError('Sex preparation must precede genotype/truth generation')
    path = directory / 'pedigree.file'
    lines = path.read_text().splitlines(True)
    animals = {int(row.split()[1]): row.split() for row in lines}
    if len(animals) != len(lines):
        raise ValueError('Duplicate pedigree IDs')
    required = {}
    for row in animals.values():
        for column, sex in ((2, 'M'), (3, 'F')):
            parent = int(row[column])
            if parent <= 0:
                continue
            if parent not in animals:
                raise ValueError('Unknown positive parent ID')
            if parent in required and required[parent] != sex:
                raise ValueError('Animal is used as both sire and dam; cannot repair sex alone')
            required[parent] = sex
    changed = {a:sex for a,sex in required.items() if animals[a][0] != sex}
    before = {'pedigree.file': identity(path)}
    for i, line in enumerate(lines):
        animal = int(line.split()[1])
        if animal in changed:
            start = len(line) - len(line.lstrip())
            lines[i] = line[:start] + changed[animal] + line[start+1:]
    path.write_text(''.join(lines))
    # These pedsim metadata products also contain sex. Genosim reads pedigree.file,
    # but keep metadata internally consistent before its generation starts.
    for name in ('phenotype.data0', 'phenotype.later0'):
        target = directory / name
        if not target.exists():
            continue
        before[name] = identity(target)
        def replace(match):
            animal = int(match.group(1))
            return match.group(1) + match.group(2) + changed.get(animal, match.group(3))
        target.write_text(re.sub(r'^(\s*\d+)(\s+)([MF])(?=\s)', replace,
                                 target.read_text(), flags=re.MULTILINE))
    return {'policy':'sex from unique parental role before genosim; reject dual-role parents',
            'changed_animals':len(changed), 'changed_to_female':sum(x == 'F' for x in changed.values()),
            'changed_to_male':sum(x == 'M' for x in changed.values()),
            'before':before, 'after':{name:identity(directory/name) for name in before}}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    print(json.dumps(prepare_parent_sexes(args.directory), indent=2))
