"""Check real row-policy dispatch and exact public outputs, including limits."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def identity(path):
    with path.open('rb') as stream:
        return dict(bytes=path.stat().st_size,sha256=hashlib.file_digest(stream,'sha256').hexdigest())


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary',required=True,type=Path)
    ap.add_argument('--record',type=Path)
    a=ap.parse_args();binary=a.binary.resolve()
    example=Path(__file__).resolve().parents[1]/'examples/small'
    expected=json.loads((example/'expected.json').read_text())
    base={k:v for k,v in os.environ.items() if not k.startswith(('OMP_','GOMP_'))}
    base.update(LC_ALL='C',OMP_NUM_THREADS='20',OMP_DYNAMIC='FALSE')
    cases=[(f'auto-{n}',[str(n)],{},None,None) for n in range(1,21)]
    cases += [
        ('forced-static',['20','--row-schedule=static'],{},None,'static'),
        ('forced-dynamic',['--row-schedule=dynamic','10'],{},None,'dynamic'),
        ('no-position',[],{'OMP_NUM_THREADS':'11'},None,None),
        ('num-threads-cap',['20'],{'OMP_NUM_THREADS':'10'},None,'static'),
        ('thread-limit-cap',['11'],{'OMP_THREAD_LIMIT':'10'},None,'static'),
        ('dynamic-team',['11'],{'OMP_DYNAMIC':'TRUE'},None,'static'),
        ('explicit-placement',['11'],{'OMP_PROC_BIND':'FALSE'},None,'static'),
        ('explicit-runtime-schedule',['11'],{'OMP_SCHEDULE':'dynamic,2'},None,'static'),
        ('inherit-placement',['20','--cpu-policy=inherit'],{},None,None),
        ('restricted-allocation',['20'],{}, {min(os.sched_getaffinity(0))},'static'),
    ]
    records=[]
    with tempfile.TemporaryDirectory(prefix='turbohap-row-dispatch-') as temp:
        for name,args,overrides,mask,schedule in cases:
            for mode in ['standard','lm']:
                environment=dict(base,**overrides)
                flags=args+(['--low-memory'] if mode=='lm' else [])
                def invoke(extra=()):
                    return subprocess.run([str(binary),*flags,*extra],env=environment,cwd=directory,
                        capture_output=True,text=True,timeout=20,
                        preexec_fn=(lambda:os.sched_setaffinity(0,mask)) if mask else None)
                directory=Path(temp)/(name+'-'+mode);directory.mkdir()
                info=invoke(['--cpu-info'])
                assert info.returncode==0 and not info.stderr,(name,info.stderr)
                assert not list(directory.iterdir())
                decision=json.loads(info.stdout)['row_scheduling']
                if schedule:assert decision['schedule']==schedule,(name,decision)
                if name=='thread-limit-cap':assert decision['effective_workers']==10
                for filename,rec in expected['inputs'].items():
                    source=example/filename;assert identity(source)==rec
                    (directory/filename).symlink_to(source)
                result=invoke()
                assert result.returncode==0,(name,result.stderr)
                actual={p.name:identity(p) for p in directory.iterdir() if not p.is_symlink()}
                assert actual==expected['outputs'],(name,mode)
                indicator='TurboHap row schedule='+('dynamic,4' if decision['schedule']=='dynamic' else 'static')
                assert indicator in result.stderr,(name,decision,result.stderr)
                records.append(dict(case=name,mode=mode,schedule=decision['schedule'],
                                    effective_workers=decision['effective_workers'],raw_passed=True))
    if a.record:a.record.write_text(json.dumps(dict(passed=True,cases=records),indent=2)+'\n')
    print(f'PASS: {len(records)} real dispatch/output cases; automatic counts1–20, explicit overrides, omitted counts, thread limits, dynamic teams and restricted affinity')


if __name__=='__main__':main()
