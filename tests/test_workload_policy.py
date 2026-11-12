"""Qualified native defaults preserve the established unqualified platform policies."""
import argparse
from pathlib import Path
import subprocess

FEATURES = ('FINDHAP_PEDIGREE_VECTOR_BASE', 'FINDHAP_POP_VECTOR_SEGMENT',
            'FINDHAP_PROGENY_SEGMENT_OWNED', 'FINDHAP_LIBRARY_ACTIVE_BOUND',
            'FINDHAP_PEDIGREE_NEON_NEAR',
            'FINDHAP_LM_ADAPTIVE_POSITIONS', 'FINDHAP_PEDIGREE_EARLY_REJECT',
            'FINDHAP_POP_COMPRESS_SIGNATURE', 'FINDHAP_POP_FUSED_VALIDITY')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1] / 'src'
    linux_arm = ['__linux__=1', '__aarch64__=1', '__ARM_NEON=1']
    on = ['FINDHAP_WORKLOAD_OPTIMISATIONS=1']
    spark = linux_arm + ['FINDHAP_NATIVE_GB10=1']
    disabled = {k: 0 for k in FEATURES}
    enabled = {k: 1 for k in FEATURES}
    cases = [
        ('Spark default', spark, disabled, 0, 0),
        ('Spark opt-in', spark + on, enabled, 4, 2),
        ('zero profile marker', linux_arm + on + ['FINDHAP_NATIVE_GB10=0'], disabled, 0, 0),
        ('ARM C4A', linux_arm + on + ['FINDHAP_NATIVE_NEOVERSE_V2=1'], disabled, 0, 0),
        ('generic ARM', linux_arm + on, disabled, 0, 0),
        ('Apple Silicon', on + ['__APPLE__=1', '__aarch64__=1', '__ARM_NEON=1',
                              'FINDHAP_NATIVE_APPLE_M4PRO=1'], disabled, 0, 0),
        ('Intel C4', on + ['__linux__=1', '__x86_64__=1', '__AVX2__=1', '__graniterapids__=1'], disabled, 2, 0),
        ('AMD C4D', on + ['__linux__=1', '__x86_64__=1', '__AVX2__=1', '__znver5__=1'], disabled, 2, 0),
        ('explicit disables', spark + on + ['FINDHAP_PEDIGREE_VECTOR_BASE=0'],
             dict(enabled, FINDHAP_PEDIGREE_VECTOR_BASE=0), 4, 2),
    ]
    intel = ['__linux__=1', '__x86_64__=1', '__graniterapids__=1', '__AVX2__=1',
             '__AVX512BW__=1', '__AVX512VBMI__=1', '__BMI2__=1']
    intel_enabled = dict(enabled, FINDHAP_PEDIGREE_NEON_NEAR=0,
                         FINDHAP_POP_COMPRESS_SIGNATURE=0)
    cases.extend([
        ('qualified Intel default', intel, intel_enabled, 2, 2),
        ('qualified Intel opt-in', intel + on, intel_enabled, 2, 2),
        ('qualified Intel override', intel + ['FINDHAP_PEDIGREE_VECTOR_BASE=0'],
             dict(intel_enabled, FINDHAP_PEDIGREE_VECTOR_BASE=0), 2, 2),
    ])
    for name, definitions, expected, backend, order in cases:
        expected = dict(expected, FINDHAP_PEDIGREE_MASK_BACKEND=backend,
                        FINDHAP_ORDERED_SCAN_ORDER=order,
                        FINDHAP_LIBRARY_METADATA=int(name in ('Spark default', 'Spark opt-in',
                                                              'explicit disables') or name.startswith('qualified Intel')),
                        FINDHAP_METADATA_CALLOC=1, FINDHAP_METADATA_CHUNK=1)
        native = name.startswith('qualified Intel')
        expected.update(FINDHAP_POP_CONFLICT_BACKEND=2 if native else 0,
            FINDHAP_PEDIGREE_CONFLICT_BACKEND=3 if native else 0,
            FINDHAP_POP_SEGMENT_BACKEND=2 if native else 0,
            FINDHAP_PEDIGREE_FREQUENCY_BACKEND=2 if native else 0,
            FINDHAP_POP_X86_PEXT=1 if native else 0)
        text = '#include "platform_policy.hpp"\n'
        for key, value in expected.items():
            text += f'#if {key} == {value}\nPASS_{key}\n#else\nFAIL_{key}\n#endif\n'
        command = [args.compiler, '-E', '-P', '-undef', '-nostdinc', '-x', 'c++',
                   '-I', str(source), *['-D' + d for d in definitions], '-']
        result = subprocess.run(command, input=text, text=True, capture_output=True, check=True)
        if 'FAIL_' in result.stdout or result.stdout.count('PASS_') != len(expected):
            raise RuntimeError(name + ': ' + result.stdout)
    # The allocation policy is independent of engine, capacity and workload
    # opt-in, while an explicit disabled control remains available.
    allocation_cases = [
        ('standard capacity 50000', spark + ['FINDHAP_HIGH_MEMORY=1', 'TEST_MAXHAP=50000'], 1),
        ('LM capacity 65535', spark + ['FINDHAP_LOW_MEMORY=1', 'TEST_MAXHAP=65535'], 1),
        ('LM capacity 500000', spark + ['FINDHAP_LOW_MEMORY=1', 'TEST_MAXHAP=500000'], 1),
        ('explicit allocation off', spark + on + ['FINDHAP_LIBRARY_METADATA=0'], 0),
        ('Intel standard capacity 50000', intel + ['FINDHAP_HIGH_MEMORY=1', 'TEST_MAXHAP=50000'], 1),
        ('Intel LM capacity 500000', intel + ['FINDHAP_LOW_MEMORY=1', 'TEST_MAXHAP=500000'], 1),
        ('Intel explicit allocation off', intel + ['FINDHAP_LIBRARY_METADATA=0'], 0),
    ]
    for name, definitions, expected in allocation_cases:
        text = ('#include "platform_policy.hpp"\n'
                f'#if FINDHAP_LIBRARY_METADATA == {expected}\nPASS\n#else\nFAIL\n#endif\n')
        command = [args.compiler, '-E', '-P', '-undef', '-nostdinc', '-x', 'c++',
                   '-I', str(source), *['-D' + d for d in definitions], '-']
        result = subprocess.run(command, input=text, text=True, capture_output=True, check=True)
        if result.stdout.strip() != 'PASS':
            raise RuntimeError(name + ': ' + result.stdout)
    print(f'PASS: {len(cases) + len(allocation_cases)} workload-policy cases')


if __name__ == '__main__':
    main()
