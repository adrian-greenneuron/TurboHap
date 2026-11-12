#!/usr/bin/env python3
"""Independently check native Turin selection, missing ISA guards and overrides."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--header', type=Path)
    args = parser.parse_args()
    header = args.header or Path(__file__).resolve().parents[1]/'src/platform_policy.hpp'
    full = dict(TURBOHAP_NATIVE_AMD_WORKLOAD_PROFILE=1, TURBOHAP_NATIVE_INTEL_WORKLOAD_PROFILE=0,
        TURBOHAP_NATIVE_X86_WORKLOAD_PROFILE=1, TURBOHAP_NEON_WORKLOAD_PROFILE=0,
        TURBOHAP_WORKLOAD_PROFILE=1, FINDHAP_LIBRARY_METADATA=1,
        FINDHAP_PEDIGREE_VECTOR_BASE=1, FINDHAP_POP_VECTOR_SEGMENT=1,
        FINDHAP_POP_COMPRESS_SIGNATURE=0, FINDHAP_POP_FUSED_VALIDITY=1,
        FINDHAP_PROGENY_SEGMENT_OWNED=1, FINDHAP_LIBRARY_ACTIVE_BOUND=1,
        FINDHAP_PEDIGREE_NEON_NEAR=0, FINDHAP_POP_CONFLICT_BACKEND=2,
        FINDHAP_PEDIGREE_CONFLICT_BACKEND=3, FINDHAP_POP_SEGMENT_BACKEND=2,
        FINDHAP_PEDIGREE_FREQUENCY_BACKEND=2, FINDHAP_POP_X86_PEXT=1,
        FINDHAP_LM_ADAPTIVE_POSITIONS=1, FINDHAP_PEDIGREE_EARLY_REJECT=1,
        FINDHAP_ORDERED_SCAN_ORDER=2)
    off = {key:0 for key in full}
    native = ['__linux__=1', '__x86_64__=1', '__znver4__=1', '__AVX512VP2INTERSECT__=1',
              '__AVX2__=1', '__AVX512F__=1', '__AVX512BW__=1', '__AVX512VBMI__=1', '__BMI2__=1']
    cases = [('GCC13 Turin', native, full),
        ('znver5 Turin', [x for x in native if not x.startswith('__znver4__')]+['__znver5__=1'], full),
        ('native default independent of opt-in', native+['FINDHAP_WORKLOAD_OPTIMISATIONS=0'], full)]
    for definition in native:
        cases.append(('missing '+definition, [x for x in native if x!=definition], off))
    for name, marker in [('Milan', '__znver3__'), ('Cascade Lake', '__cascadelake__')]:
        cases.append((name, [x for x in native if not x.startswith('__znver4__')]+[marker+'=1'], off))
    for key in [key for key in full if key.startswith('FINDHAP_')]:
        expected = dict(full, **{key:0})
        cases.append(('explicit disable '+key, native+[key+'=0'], expected))
    for name, definitions, expected in cases:
        text = '#include "'+str(header)+'"\n'
        for key, value in expected.items():
            text += f'#if {key} == {value}\nPASS_{key}\n#else\nFAIL_{key}\n#endif\n'
        result = subprocess.run([args.compiler, '-E', '-P', '-undef', '-nostdinc', '-x', 'c++',
            *['-D'+item for item in definitions], '-'], input=text, text=True, capture_output=True, check=True)
        if 'FAIL_' in result.stdout or result.stdout.count('PASS_') != len(expected):
            raise RuntimeError(name+': '+result.stdout)
    print(f'PASS: {len(cases)} independent Turin profile/ISA/override cases')


if __name__ == '__main__':
    main()
