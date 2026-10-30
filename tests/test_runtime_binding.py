"""Check the pre-engine bootstrap with stub engines; no dataset required."""
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    source=Path(__file__).resolve().parents[1]/'src/launcher.cpp'
    with tempfile.TemporaryDirectory(prefix='turbohap-binding-') as directory:
        root=Path(directory)
        stub=root/'engines.cpp'
        stub.write_text(r'''
#include <cstdio>
#include <cstdlib>
#include <omp.h>
#include <sched.h>
#include <unistd.h>
static int report(const char* mode,int argc,char** argv) {
    cpu_set_t mask;
    if(sched_getaffinity(0,sizeof(mask),&mask))return 98;
    int allowed=std::atoi(std::getenv("TURBOHAP_TEST_CPU"));
    if(CPU_COUNT(&mask)!=1 || !CPU_ISSET(allowed,&mask))return 97;
    std::printf("%ld\n%s\n%s\n%s\n%s\n",long(getpid()),mode,
        std::getenv("OMP_PROC_BIND")?std::getenv("OMP_PROC_BIND"):"unset",
        std::getenv("OMP_PLACES")?std::getenv("OMP_PLACES"):"unset",
        omp_get_proc_bind()==omp_proc_bind_false?"unbound":"bound");
    for(int i=1;i<argc;++i)std::puts(argv[i]);
    return argv[argc]==nullptr?0:99;
}
namespace findhap_standard {int run(int argc,char** argv){return report("standard",argc,argv);}}
namespace findhap_low_memory {int run(int argc,char** argv){return report("lm",argc,argv);}}
''')
        allowed_cpu=min(os.sched_getaffinity(0))
        checks=0
        for enabled in [0,1]:
            binary=root/('launcher-'+str(enabled))
            subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O2',
                '-fopenmp',
                '-DFINDHAP_TURIN_BINDING='+str(enabled),str(source),str(stub),'-o',str(binary)],
                check=True,capture_output=True)
            for overrides,explicit_expected in [({},None),
                ({'OMP_PROC_BIND':'false'},('false','unset')),
                ({'OMP_PLACES':'threads'},('unset','threads')),
                ({'OMP_PROC_BIND':'spread','OMP_PLACES':'cores'},('spread','cores')),
                ({'GOMP_CPU_AFFINITY':str(allowed_cpu)},('unset','unset'))]:
                environment={k:v for k,v in os.environ.items() if k not in ['OMP_PROC_BIND','OMP_PLACES','GOMP_CPU_AFFINITY']}
                environment.update(overrides)
                environment['TURBOHAP_TEST_CPU']=str(allowed_cpu)
                for inherit in [False,True]:
                    automatic=enabled and not inherit and not overrides
                    expected=explicit_expected or (('close','cores') if automatic else ('unset','unset'))
                    bound=automatic or (overrides and overrides.get('OMP_PROC_BIND')!='false')
                    for flags,mode in [([], 'standard'),(['--low-memory'],'lm')]:
                        policy=['--cpu-policy=inherit'] if inherit else []
                        child=subprocess.Popen([str(binary),'10',*policy,*flags,'R'],stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,text=True,env=environment,
                            preexec_fn=lambda:os.sched_setaffinity(0,{allowed_cpu}))
                        stdout,stderr=child.communicate(timeout=20)
                        assert child.returncode==0 and stderr.startswith('TurboHap CPU policy='),(stdout,stderr)
                        assert stdout.splitlines()==[str(child.pid),mode,*expected,
                            'bound' if bound else 'unbound','10','R'],stdout
                        checks+=1
        print(f'PASS: {checks} launches; same-PID bootstrap, actual OpenMP binding, both modes, argument forwarding, inherit/explicit overrides and restricted CPU masks')


if __name__=='__main__':main()
