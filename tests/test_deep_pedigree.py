#!/usr/bin/env python3
"""Generated Mendelian deep-pedigree integration fixtures; strict Fortran bytes.

These fixtures test compatibility, not the causal runtime effect of depth or
structural accuracy. Family widths and founder fractions differ with depth.
Founder haplotypes and transmitted/recombined truth are generated together with
the pedigree, never by changing parents on pre-existing genotype data.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import subprocess


def identity(path):
    with path.open('rb') as f:
        return dict(bytes=path.stat().st_size, sha256=hashlib.file_digest(f,'sha256').hexdigest())


def identities(directory, exclude=()):
    result = {}
    for p in sorted(directory.iterdir()):
        if p.is_file() and p.name not in exclude:
            result[p.name] = identity(p)
    return result


def generate(directory, levels, coverage, seed):
    rng = random.Random(seed)
    n, chromosomes, per_chromosome = 612, 20, 120
    loci = chromosomes * per_chromosome
    assert n % levels == 0 and (n // levels) % 2 == 0
    width = n // levels
    pedigree, truth = [], []
    for animal in range(n):
        generation = animal // width
        if generation:
            previous = (generation - 1) * width
            sire = previous + 2 * rng.randrange(width // 2)
            dam = previous + 2 * rng.randrange(width // 2) + 1
            inherited = []
            for parent in [sire,dam]:
                hap = bytearray(loci)
                for chromosome in range(chromosomes):
                    state = rng.randrange(2)
                    for l in range(chromosome * per_chromosome, (chromosome + 1) * per_chromosome):
                        if rng.random() < .01: state = 1 - state
                        hap[l] = truth[parent][state][l]
                inherited.append(hap)
        else:
            sire = dam = -1
            inherited = [bytearray(int(rng.random() < .35) for _ in range(loci)) for _ in range(2)]
        pedigree.append((sire,dam,generation))
        truth.append(inherited)
    with (directory/'pedigree.file').open('w') as f:
        for animal,(sire,dam,generation) in enumerate(pedigree):
            f.write(f'{"M" if animal%2==0 else "F"} {10001+animal} '
                    f'{10001+sire if sire>=0 else 0} {10001+dam if dam>=0 else 0} '
                    f'{20000101+generation*10000}\n')
    typed = 0
    with (directory/'genotypes.txt').open('w') as f:
        for animal in range(n):
            if animal // width != levels-1 and rng.random() >= coverage: continue
            typed += 1
            genotype=[]
            for l in range(loci):
                value=truth[animal][0][l]+truth[animal][1][l]
                u=rng.random()
                if u < .02: value=5
                elif u < .022: value=(value+1)%3
                genotype.append(str(value))
            f.write(f'{10001+animal:10d}{1:10d}{loci:10d} '+''.join(genotype)+'\n')
    with (directory/'chromosome.data').open('w') as f:
        f.write('SNPname chrome within overall location n_chips chip1\n')
        for l in range(loci):
            chromosome=l//per_chromosome+1;within=l%per_chromosome+1
            f.write(f'Marker{l+1:09d} {chromosome} {within} {l+1} {within*10000} 1 {l+1}\n')
    (directory/'findhap.options').write_text('4 0 100 50 3 50000 1 1 1 1\nlabels\n\n.004\n')
    with (directory/'simulation-truth.bin').open('wb') as f:
        for pair in truth:
            for hap in pair:f.write(hap)
    return dict(seed=seed, levels=levels, maximum_depth=levels-1, pedigree_animals=n,
                genotyped_animals=typed, earlier_generation_genotype_probability=coverage,
                loci=loci, recombination_probability_per_marker=.01,
                missing_genotype_probability=.02, error_genotype_probability=.002,
                truth_layout='animal-major, two haplotypes of loci bytes each; alleles 0/1')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ['reference','control','candidate','work','record']:ap.add_argument('--'+name,type=Path,required=True)
    a=ap.parse_args(); a.work.mkdir(parents=True,exist_ok=False)
    records=[]; inputs=['pedigree.file','genotypes.txt','chromosome.data','findhap.options']
    for levels in [3,9,17]:
        for coverage in [.25,.75]:
            case=f'depth{levels-1}-coverage{int(coverage*100)}';base=a.work/case;base.mkdir()
            meta=generate(base,levels,coverage,291026+levels*100+int(coverage*100))
            (base/'metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
            reference=None
            conditions=[('fortran',a.reference,1,[]),('control',a.control,1,[])]
            conditions += [(mode,a.candidate,t,flags) for mode,flags in [('standard',[]),('lm',['--low-memory'])] for t in [1,4,10]]
            for label,binary,threads,flags in conditions:
                directory=base/f'{label}-{threads}';directory.mkdir()
                for name in inputs:shutil.copy2(base/name,directory/name)
                env={k:v for k,v in os.environ.items() if not k.startswith(('OMP_','GOMP_'))}
                env.update(LC_ALL='C',OMP_NUM_THREADS=str(threads),OMP_DYNAMIC='FALSE')
                result=subprocess.run([str(binary.resolve()),str(threads),*flags],cwd=directory,env=env,
                                      capture_output=True,timeout=180)
                (base/f'{label}-{threads}.log').write_bytes(result.stdout+result.stderr)
                assert result.returncode==0 and b'Program findhap completed normally' in result.stdout,(case,label,threads,result.returncode)
                actual=identities(directory,inputs)
                if reference is None:reference=actual
                passed=actual==reference
                records.append(dict(case=case,label=label,threads=threads,passed=passed,metadata=meta,
                                    outputs=actual,inputs={n:identity(base/n) for n in inputs+['simulation-truth.bin']}))
                a.record.write_text(json.dumps(records,indent=2)+'\n')
                assert passed,(case,label,threads,[k for k in actual if actual[k]!=reference.get(k)])
                shutil.rmtree(directory)
            print('PASS',case,'every file byte identical to one-thread GNU FindHap',flush=True)


if __name__=='__main__':main()
