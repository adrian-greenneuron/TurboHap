#!/usr/bin/env python3
"""Exact-output comparison across pedigree options, modes and thread counts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--reference',type=Path,required=True)
    ap.add_argument('--candidate',type=Path,required=True)
    ap.add_argument('--record',type=Path,required=True)
    ap.add_argument('--case',help='Run just one named case')
    ap.add_argument('--reference-single-thread',action='store_true',help='Use the legacy serial execution as the oracle')
    a=ap.parse_args()
    example=Path(__file__).resolve().parents[1]/'examples/small'
    inputs=json.loads((example/'expected.json').read_text())['inputs']
    cases={
        'default':([4,30,100,50,3,50000,1,1,1,1],'.004'),
        'two_iterations':([2,30,100,50,3,50000,1,1,1,1],'.004'),
        'single_pass':([1,30,100,50,1,50000,1,1,1,1],'.004'),
        'zero_error':([4,30,100,50,3,50000,1,1,1,1],'0'),
        'more_error':([4,30,100,50,3,50000,1,1,1,1],'.02'),
        'both_parents':([4,30,100,50,3,50000,2,2,2,-1],'.004'),
        'no_parent_imputation':([4,30,100,50,3,50000,0,5,0,0],'.004'),
        'single_chromosome':([4,0,100,50,3,50000,1,1,1,1],'.004')}
    results=[]
    if a.case:cases={a.case:cases[a.case]}
    with tempfile.TemporaryDirectory(prefix='pedigree-differential-') as tmp:
        for case,(values,error) in cases.items():
            for mode,flags in [('standard',[]),('lm',['--low-memory'])]:
                reference=None
                for label,binary in [('reference',a.reference),('candidate',a.candidate)]:
                    for threads in ([1] if label=='reference' and a.reference_single_thread else [1,4,10]):
                        root=Path(tmp)/f'{case}-{mode}-{label}-{threads}';root.mkdir()
                        for f in inputs:shutil.copy2(example/f,root/f)
                        if case=='single_chromosome':
                            lines=(root/'chromosome.data').read_text().splitlines()
                            rows=[lines[0]]
                            for line in lines[1:]:
                                fields=line.split();fields[1]='1';fields[2]=fields[3];fields[4]=str(int(fields[3])*10000)
                                rows.append(' '.join(fields))
                            (root/'chromosome.data').write_text('\n'.join(rows)+'\n')
                        (root/'findhap.options').write_text(' '.join(map(str,values))+'\nlabels\n\n'+error+'\n')
                        p=subprocess.run([str(binary.resolve()),str(threads),*flags],cwd=root,capture_output=True,
                            env=dict(os.environ,OMP_NUM_THREADS=str(threads),OMP_DYNAMIC='FALSE',LC_ALL='C'))
                        if p.returncode or b'Program findhap completed normally' not in p.stdout:
                            raise RuntimeError(f'{case}/{mode}/{label}/{threads}: {p.returncode}: '+p.stderr.decode())
                        outputs={f.name:{'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
                                 for f in root.iterdir() if f.name not in inputs}
                        if reference is None:reference=outputs;reference_dir=root
                        passed=outputs==reference
                        results.append(dict(case=case,mode=mode,label=label,threads=threads,outputs=outputs,passed=passed))
                        a.record.write_text(json.dumps(results,indent=2)+'\n')
                        if not passed:
                            failure=a.record.with_suffix('.failure')
                            shutil.copytree(reference_dir,failure/'reference')
                            shutil.copytree(root,failure/'candidate')
                            raise AssertionError((case,mode,label,threads,[k for k in outputs if outputs[k]!=reference.get(k)]))
                print('PASS',case,mode,flush=True)
    a.record.write_text(json.dumps(results,indent=2)+'\n')

if __name__=='__main__':main()
