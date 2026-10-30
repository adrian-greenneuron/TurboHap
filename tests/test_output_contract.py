#!/usr/bin/env python3
"""Focused output combinations, priors, chunk boundaries and I/O failure checks."""
import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def identities(root,inputs):
    return {p.name:dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
            for p in root.iterdir() if p.is_file() and p.name not in inputs}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reference',type=Path,help='Optional frozen control; otherwise use committed reference identities')
    ap.add_argument('--candidate',type=Path,required=True)
    ap.add_argument('--record',type=Path,required=True)
    a=ap.parse_args();source=Path(__file__).resolve().parents[1];example=source/'examples/small'
    expected=json.loads((example/'expected.json').read_text());records=[]
    golden=json.loads((source/'tests/reference_output_contract.json').read_text()) if not a.reference else None
    env={k:v for k,v in os.environ.items() if not k.startswith(('OMP_','GOMP_'))}
    env.update(OMP_NUM_THREADS='4',OMP_DYNAMIC='FALSE',LC_ALL='C')
    def save():a.record.write_text(json.dumps(records,indent=2)+'\n')
    def setup(root,case,options):
        root.mkdir()
        for n in expected['inputs']:shutil.copy2(example/n,root/n)
        (root/'findhap.options').write_text(' '.join(map(str,options))+'\nlabels\n\n.004\n')
        if case=='partial_mixed':
            ped=(root/'pedigree.file').read_text().splitlines();geno=(root/'genotypes.txt').read_text().splitlines()
            template=geno[0];fields=template.split();loci=int(fields[2])
            for i in range(1003-len(ped)):
                ident=1000000+i;ped.append(f'M {ident} -2 -1 20000101 {ident} {ident}')
                row=f'{ident:10d}'+template[10:]
                if i%2:row=row[:-loci]+'5'*loci
                geno.append(row)
            (root/'pedigree.file').write_text('\n'.join(ped)+'\n')
            (root/'genotypes.txt').write_text('\n'.join(geno)+'\n')
        if case in ['no_usable','existing_empty']:
            lines=(root/'genotypes.txt').read_text().splitlines()
            (root/'genotypes.txt').write_text(''.join(line[:-int(line.split()[2])]+'5'*int(line.split()[2])+'\n' for line in lines))
        if case in ['existing','existing_empty']:
            for name in expected['outputs']:(root/name).write_bytes(b'RETAIN-IF-EMPTY\n'*10000)
    def run(binary,root,mode,extra=None):
        return subprocess.run([str(binary.resolve()),'4',*(['--low-memory'] if mode=='lm' else [])],cwd=root,
            env=dict(env,**(extra or {})),capture_output=True,timeout=40)
    with tempfile.TemporaryDirectory(prefix='turbohap-output-contract-') as tmp:
        base=Path(tmp)
        fault=base/'stdio_fault.so'
        subprocess.run([os.environ.get('CC',os.environ.get('CXX','cc')),'-x','c','-O2','-shared','-fPIC',source/'tests/stdio_fault.c','-ldl','-o',fault],check=True)
        cases=[('outputs_'+''.join(map(str,c)),[4,30,100,50,3,50000,c[0],c[1],1,c[2]])
               for c in itertools.product([0,1],repeat=3)]
        cases += [(name,[4,30,100,50,3,50000,1,1,1,1]) for name in
                  ['partial_mixed','no_usable','existing','existing_empty','priors']]
        for case,options in cases:
            for mode in ['standard','lm']:
                results={}
                priors=None
                if case=='priors':
                    seed=base/f'seed-{mode}';setup(seed,'default',options)
                    result=run(a.reference or a.candidate,seed,mode)
                    assert result.returncode==0
                    priors={'hap.listpriors':(seed/'hap.list').read_bytes(),
                            'hap.foundpriors':(seed/'hap.found').read_bytes(),'hap.Xseg':(seed/'hap.Xseg').read_bytes()}
                binaries=([('reference',a.reference)] if a.reference else [])+[('candidate',a.candidate)]
                for label,binary in binaries:
                    root=base/f'{case}-{mode}-{label}';setup(root,case,options)
                    if priors:
                        for name,data in priors.items():(root/name).write_bytes(data)
                    result=run(binary,root,mode)
                    inputs=set(expected['inputs'])|{'hap.listpriors','hap.foundpriors'}
                    outputs=identities(root,inputs)
                    assert result.returncode==0 and b'Program findhap completed normally' in result.stdout,(case,mode,label,result.stderr)
                    if case=='no_usable':assert outputs['genotypes.filled']['bytes']==outputs['haplotypes.txt']['bytes']==0
                    if case=='existing_empty':
                        assert (root/'genotypes.filled').read_bytes()==b'RETAIN-IF-EMPTY\n'*10000
                    results[label]=outputs
                if golden:results['reference']=golden['outputs'][case+'/'+mode]
                passed=results['reference']==results['candidate']
                records.append(dict(case=case,mode=mode,passed=passed,outputs=results));save()
                assert passed,(case,mode)
                print('PASS',case,mode,flush=True)
        for mode in ['standard','lm']:
            for stage in ['write','flush','close','open']:
                for label,binary in ([('reference',a.reference)] if a.reference else [])+[('candidate',a.candidate)]:
                    root=base/f'fault-{stage}-{mode}-{label}';setup(root,'default',[4,30,100,50,3,50000,1,1,1,1])
                    extra=dict(LD_PRELOAD=str(fault),TURBOHAP_FAULT_FILE='hap.inherit',TURBOHAP_FAULT_STAGE=stage)
                    if stage=='open':(root/'hap.inherit').mkdir();extra={}
                    result=run(binary,root,mode,extra)
                    passed=result.returncode!=0 and b'Program findhap completed normally' not in result.stdout
                    records.append(dict(case='fault_'+stage,mode=mode,label=label,passed=passed,
                        exit_status=result.returncode,reported_success=b'Program findhap completed normally' in result.stdout,
                        stderr=result.stderr.decode(errors='replace')));save()
                    print('PASS' if passed else 'FAIL',stage,mode,label,result.returncode,flush=True)
        failures=[r for r in records if not r['passed'] and r.get('label')!='reference']
        assert not failures,failures
    print('PASS: output/priors/chunk contracts and candidate error propagation')


if __name__=='__main__':main()
