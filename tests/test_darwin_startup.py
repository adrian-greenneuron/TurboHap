"""Verify Darwin defaults, real worker QoS, team reuse and explicit overrides."""
import json
import platform
import os
from pathlib import Path
import subprocess
import tempfile

from fixture_build import build_fixture

ROOT = Path(__file__).resolve().parents[1]
STUB = r'''
#include <cstdio>
#include <cstdlib>
#include <omp.h>
#include "thread_request.hpp"
#include "macos_policy.hpp"
static int report(const char* mode) {
    int size = 0;
    const int requested = findhap::startup_thread_request.effective;
    for (int count : {1, std::min(4, requested), requested, 1, requested}) {
        #pragma omp parallel num_threads(count)
        {
            findhap::macos::worker_enter();
            #pragma omp single
            size = omp_get_num_threads();
        }
    }
    const char* binding = std::getenv("OMP_PROC_BIND");
    const char* places = std::getenv("OMP_PLACES");
    std::printf("{\"mode\":\"%s\",\"requested\":%d,\"team\":%d,"
                "\"binding\":\"%s\",\"places\":\"%s\"}\n",
                mode, requested, size, binding ? binding : "unset", places ? places : "unset");
    return 0;
}
namespace findhap_standard { int run(int,char**) { return report("standard"); } }
namespace findhap_low_memory { int run(int,char**) { return report("low-memory"); } }
namespace findhap_dynamic_standard { int run(int,char**) { return report("dynamic-standard"); } }
namespace findhap_dynamic_low_memory { int run(int,char**) { return report("dynamic-low-memory"); } }
'''


def main():
    if platform.machine() not in ['arm64','aarch64']:
        print('SKIP: Apple Silicon QoS fixture; Intel Mac scheduling is inherited')
        return
    env = {k: v for k, v in os.environ.items() if not k.startswith(('OMP_', 'GOMP_', 'KMP_', 'FINDHAP_'))}
    env.update(LC_ALL='C', OMP_NUM_THREADS='12', OMP_DYNAMIC='FALSE')
    with tempfile.TemporaryDirectory(prefix='turbohap-darwin-startup-') as directory:
        root = Path(directory)
        stub, binary = root / 'engines.cpp', root / 'findhap'
        stub.write_text(STUB)
        # Even an explicitly forced Linux profile must never exec /proc on Darwin.
        build_fixture([ROOT/'src/launcher.cpp', stub], binary,
                      ['-DFINDHAP_TURIN_BINDING=1', '-I' + str(ROOT/'src')],
                      capture_output=False)
        cases = 0
        for mode, flags in [('standard', []), ('low-memory', ['--low-memory'])]:
            for threads, limit in [(1, 12), (4, 12), (12, 12), (12, 3)]:
                for schedule in ['auto', 'static', 'dynamic']:
                    for binding in [None, 'FALSE']:
                        case_env = dict(env, OMP_THREAD_LIMIT=str(limit), FINDHAP_WORKER_TRACE_PATH=str(root/'workers.jsonl'))
                        if binding is not None:
                            case_env['OMP_PROC_BIND'] = binding
                        result = subprocess.run([str(binary), str(threads), *flags,
                            '--row-schedule=' + schedule], cwd=root, env=case_env,
                            check=True, capture_output=True, text=True, timeout=15)
                        info = json.loads(result.stdout)
                        assert info == dict(mode=('dynamic-' if schedule == 'dynamic' else '') + mode,
                            requested=threads, team=min(threads, limit),
                            binding=binding or 'unset', places='unset'), info
                        trace = root/'workers.jsonl'
                        if binding is None:
                            workers = [json.loads(line) for line in trace.read_text().splitlines()]
                            assert all(w['qos'] == 25 and w['qos_error'] == 0 for w in workers), workers
                            assert {w['worker'] for w in workers if w['team'] == min(threads, limit)} == set(range(min(threads, limit)))
                            trace.unlink()
                        else:
                            assert not trace.exists(), 'caller placement must suppress automatic QoS'
                        cases += 1
        info = json.loads(subprocess.check_output([str(binary), '12', '--cpu-info'],
                                                  env=env, cwd=root, text=True))
        assert info['macos']['performance_physical_cores'] > 0
        assert info['macos']['qos_proposed'] == 'user-initiated' and not info['macos']['hard_affinity']
        assert not info['qualified'] and info['backend'] == 'Darwin QoS (advisory)'
        assert info['effective_threads'] == 12 and info['row_scheduling']['schedule'] == 'static'
        result = subprocess.run([str(binary), '4', '--cpu-policy=performance'],
                                env=env, cwd=root, capture_output=True, text=True)
        assert result.returncode == 2 and not result.stdout
        natural = dict(env); natural.pop('OMP_NUM_THREADS')
        for policy in ['auto','inherit']:
            data = json.loads(subprocess.check_output([str(binary), '--cpu-info', '--cpu-policy='+policy], env=natural, text=True))
            expected=data['macos']['performance_physical_cores'] if policy=='auto' else data['openmp_limit']
            assert data['effective_threads'] == min(data['openmp_limit'], expected), data
            assert data['macos']['qos_proposed'] == ('user-initiated' if policy == 'auto' else 'inherit')
        # --cpu-info must not change the caller thread QoS or create trace files.
        assert {p.name for p in root.iterdir()} == {'engines.cpp', 'findhap'}
        print(f'PASS: {cases} Darwin mode/schedule/team/limit/QoS/caller-setting cases; forced Linux bootstrap excluded')


if __name__ == '__main__':
    main()
