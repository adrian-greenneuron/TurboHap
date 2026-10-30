"""Check real OpenMP worker masks, requests and caller precedence on Spark."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--record', type=Path)
    a = ap.parse_args()
    source = Path(__file__).resolve().parents[1] / 'src'
    env = {k: v for k, v in os.environ.items() if k not in
           ['OMP_PROC_BIND', 'OMP_PLACES', 'GOMP_CPU_AFFINITY', 'OMP_THREAD_LIMIT']}
    env.update(OMP_NUM_THREADS='20', OMP_DYNAMIC='FALSE', LC_ALL='C')
    info = json.loads(subprocess.check_output([str(a.binary.resolve()), '10', '--cpu-info'], env=env, text=True))
    if not info['qualified']:
        print('SKIP: real worker-mask qualification requires the native Spark backend')
        return

    def ids(text):
        result = set()
        for field in text.split(','):
            ends = field.split('-')
            result.update(range(int(ends[0]), int(ends[-1]) + 1))
        return result

    permitted, fast = ids(info['calling_thread_cpus']), ids(info['proposed_cpus'])
    if len(permitted) != 20 or len(fast) != 10 or fast == permitted:
        print('SKIP: full Spark allocation is required for the hardware qualification')
        return
    assert len({c['kind'] for c in info['cpus'] if c['id'] in fast}) == 1
    records = []
    with tempfile.TemporaryDirectory(prefix='turbohap-cpu-workers-') as directory:
        root = Path(directory)
        stub = root / 'engines.cpp'
        stub.write_text(r'''
#include <cstdio>
#include <omp.h>
#include "thread_request.hpp"
#include "cpu_policy.hpp"
static int report(const char* mode, int argc, char** argv) {
    const auto r = findhap::startup_thread_request;
    std::printf("request %s %d %d\n", mode, r.effective, r.openmp_limit);
    if (argc > 2 && std::strcmp(argv[2], "startup-only") == 0) return 0;
    const int sizes[] = {r.effective, std::min(4, r.effective), 1, r.effective};
    for (int stage = 0; stage < 4; ++stage) {
        #pragma omp parallel num_threads(sizes[stage])
        {
            const auto mask = findhap::placement::cpu_list(findhap::placement::affinity());
            #pragma omp critical
            std::printf("worker %d %d %d %s\n", stage, omp_get_thread_num(), omp_get_num_threads(), mask.c_str());
        }
    }
    return 0;
}
namespace findhap_standard { int run(int n, char** v) { return report("standard", n, v); } }
namespace findhap_low_memory { int run(int n, char** v) { return report("lm", n, v); } }
''')
        binary = root / 'probe'
        subprocess.run([os.environ.get('CXX', 'c++'), '-O2', '-std=c++17', '-fopenmp',
                        '-DFINDHAP_NATIVE_GB10=1', '-I' + str(source), str(source / 'launcher.cpp'),
                        str(stub), '-o', str(binary)], check=True, capture_output=True)

        def run(args, expected, effective, overrides=None, mask=None):
            environment = dict(env, **(overrides or {}))
            result = subprocess.run([str(binary), *args], cwd=root, env=environment, text=True,
                                    capture_output=True, timeout=20,
                                    preexec_fn=(lambda: os.sched_setaffinity(0, mask)) if mask else None)
            assert result.returncode == 0, (args, result.stdout, result.stderr)
            rows = result.stdout.splitlines()
            assert rows[0].split()[2:] == [str(effective), environment['OMP_NUM_THREADS']], rows[0]
            workers = []
            for line in rows[1:]:
                _, stage, worker, size, cpus = line.split()
                if expected is not None:
                    assert ids(cpus) == expected, (args, line, expected)
                workers.append(dict(stage=int(stage), worker=int(worker), size=int(size), cpus=cpus))
            limit = int(environment.get('OMP_THREAD_LIMIT', 1000000))
            for stage, size in enumerate([effective, min(4, effective), 1, effective]):
                actual = [w for w in workers if w['stage'] == stage]
                assert len(actual) == min(size, limit) and {w['size'] for w in actual} == {min(size, limit)}
            records.append(dict(args=args, environment={k: environment[k] for k in environment if
                                k.startswith(('OMP_', 'GOMP_'))}, initial_mask=sorted(mask or permitted),
                                workers=workers, stderr=result.stderr, passed=True))
            return sorted((w['stage'], w['worker'], w['cpus']) for w in workers)

        for flags in [[], ['--low-memory']]:
            for n in [1, 2, 10, 11, 20]:
                run([str(n), *flags], fast if n <= 10 else permitted, n)
            run([*flags], permitted, 20)
            run(['100', *flags], fast, 10)  # Original two-character argument semantics.
            run(['10', '--cpu-policy=inherit', *flags], permitted, 10)
            run(['--cpu-policy=performance', '10', *flags], fast, 10)
            run(['10', *flags], fast, 4, {'OMP_NUM_THREADS': '4'})
            run(['10', *flags], fast, 10, {'OMP_THREAD_LIMIT': '4'})
            restricted = {min(fast), min(permitted - fast)}
            run(['10', *flags], restricted, 10, mask=restricted)
            run(['1', *flags], {min(permitted)}, 1, mask={min(permitted)})
            run(['10', *flags], permitted, 10, {'OMP_PROC_BIND': 'FALSE'})
            for external in [{'OMP_PROC_BIND': 'close', 'OMP_PLACES': 'cores'},
                             {'GOMP_CPU_AFFINITY': ' '.join(map(str, sorted(fast)[:4]))}]:
                auto = run(['10', *flags], None, 10, external)
                inherit = run(['10', '--cpu-policy=inherit', *flags], None, 10, external)
                assert auto == inherit

        for args in [['20', '--cpu-policy=performance'], ['10', '--cpu-policy=bad'], ['0']]:
            before = set(root.iterdir())
            result = subprocess.run([str(a.binary.resolve()), *args], cwd=root, env=env,
                                    capture_output=True, text=True, timeout=20)
            assert result.returncode == 2 and not result.stdout and set(root.iterdir()) == before
        # Complete new-process startup including discovery and diagnostics;
        # this is a launcher-only screen, not a genetic inference benchmark.
        startup = []
        for policy in ['inherit', 'auto', 'auto', 'inherit'] * 10:
            started = time.monotonic()
            result = subprocess.run([str(binary), '10', '--cpu-policy=' + policy, 'startup-only'],
                                    env=env, cwd=root, text=True, capture_output=True, check=True)
            startup.append(dict(policy=policy, seconds=time.monotonic() - started))
            assert result.stdout.startswith('request standard 10 20')
        if a.record:
            a.record.write_text(json.dumps(dict(passed=True, topology=info, cases=records,
                                               startup=startup), indent=2) + '\n')
    print(f'PASS: {len(records)} real worker/team/request/override cases; 40 startup observations')


if __name__ == '__main__':
    main()
