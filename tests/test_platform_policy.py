#!/usr/bin/env python3
"""Check native feature routing without compiling or running either engine."""
import argparse
import json
from pathlib import Path
import subprocess


def preprocess(compiler, header, definitions):
    command = [compiler, '-E', '-dM', '-undef', '-nostdinc', '-x', 'c++', str(header)]
    command += ['-D' + value for value in definitions]
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    return dict(line[len('#define '):].split(' ', 1) for line in result.stdout.splitlines()
                if line.startswith('#define ') and ' ' in line[len('#define '):])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler', default='g++-13')
    ap.add_argument('--record', type=Path)
    a = ap.parse_args()
    header = Path(__file__).resolve().parents[1] / 'src/platform_policy.hpp'
    avx = ['__AVX2__=1', '__AVX512VBMI__=1', '__AVX512BW__=1']
    cases = [
        ('portable', [], 0, 0, 0),
        ('ARM_NEON', ['__ARM_NEON=1'], 1, 4194304, 0),
        ('qualified_Neoverse_V2', ['__ARM_NEON=1','FINDHAP_NATIVE_NEOVERSE_V2=1'], 1, 4194304, 4),
        ('qualified_GB10', ['__ARM_NEON=1','FINDHAP_NATIVE_GB10=1'], 1, 4194304, 4),
        ('GB10_requires_NEON', ['FINDHAP_NATIVE_GB10=1'], 0, 0, 0),
        ('Ice_Lake', avx + ['__icelake_server__=1'], 1, 4194304, 3),
        ('Granite_Rapids', avx + ['__graniterapids__=1'], 1, 4194304, 3),
        ('Turin_GCC13', avx + ['__znver4__=1', '__AVX512VP2INTERSECT__=1'], 1, 4194304, 3),
        ('Turin_znver5_profile', avx + ['__znver5__=1', '__AVX512VP2INTERSECT__=1'], 1, 4194304, 3),
        ('Genoa_without_VP2INTERSECT', avx + ['__znver4__=1'], 0, 0, 3),
        ('Milan', ['__znver3__=1', '__AVX2__=1'], 0, 0, 2),
        ('Milan_without_AVX2', ['__znver3__=1'], 0, 0, 0),
        ('Cascade_Lake', ['__cascadelake__=1', '__AVX2__=1', '__AVX512BW__=1'], 0, 0, 0),
        ('Cascade_Lake_without_AVX2', ['__cascadelake__=1'], 0, 0, 0),
        ('Milan_with_extra_VP2_flag', avx + ['__znver3__=1', '__AVX512VP2INTERSECT__=1'], 0, 0, 2),
        ('generic_VP2_not_AMD_profile', avx + ['__AVX512VP2INTERSECT__=1'], 0, 0, 3),
        ('znver5_without_required_profile', avx + ['__znver5__=1'], 0, 0, 3),
        ('explicit_disable', avx + ['__graniterapids__=1', 'FINDHAP_BUFFER_INHERIT=0', 'FINDHAP_LM_ORDERED_BYTES=0'], 0, 0, 3),
        ('explicit_binding_disable', avx+['__znver4__=1','__AVX512VP2INTERSECT__=1','FINDHAP_TURIN_BINDING=0'],1,4194304,3),
        ('explicit_Milan_binding_disable', ['__znver3__=1','__AVX2__=1','FINDHAP_TURIN_BINDING=0'],0,0,2),
        ('explicit_Cascade_Lake_binding_disable', ['__cascadelake__=1','__AVX2__=1','FINDHAP_TURIN_BINDING=0'],0,0,0),
        ('explicit_custom_budget', ['FINDHAP_BUFFER_INHERIT=1', 'FINDHAP_LM_ORDERED_BYTES=8192'], 1, 8192, 0),
        ('explicit_output_disable', avx+['__graniterapids__=1','FINDHAP_INHERIT_RECORD_OUTPUT=0',
            'FINDHAP_OUTPUT_STREAMS=0','FINDHAP_OUTPUT_BATCH_WRITE=0'],1,4194304,3),
        ('explicit_GB10_disable', ['__ARM_NEON=1','FINDHAP_NATIVE_GB10=1','FINDHAP_POP_ENCODE=0',
            'FINDHAP_INHERIT_RECORD_OUTPUT=0','FINDHAP_OUTPUT_STREAMS=0','FINDHAP_OUTPUT_BATCH_WRITE=0'],1,4194304,0),
        ('explicit_library_disable', ['__ARM_NEON=1','FINDHAP_NATIVE_GB10=1','FINDHAP_LIBRARY_FORMAT=0'],1,4194304,4),
        ('explicit_library_candidate', ['FINDHAP_LIBRARY_FORMAT=2'],0,0,0),
        ('explicit_rematch_disable', ['__ARM_NEON=1','FINDHAP_NATIVE_GB10=1','FINDHAP_PEDIGREE_REMATCH=0'],1,4194304,4),
        ('explicit_rematch_candidate', ['FINDHAP_PEDIGREE_REMATCH=2'],0,0,0),
    ]
    records = []
    for name, definitions, writer, lookup, encoder in cases:
        policy = preprocess(a.compiler, header, definitions)
        expected = dict(FINDHAP_BUFFER_INHERIT=writer, FINDHAP_LM_ORDERED_BYTES=lookup,
                        FINDHAP_POP_ENCODE=encoder, FINDHAP_POP_FULL=2 if encoder == 3 else 0,
                        FINDHAP_PEDIGREE_REMATCH=2 if '__ARM_NEON=1' in definitions and 'FINDHAP_NATIVE_GB10=1' in definitions else 0,
                        FINDHAP_LIBRARY_FORMAT=2 if 'FINDHAP_NATIVE_GB10=1' in definitions or
                          'FINDHAP_NATIVE_NEOVERSE_V2=1' in definitions or '__graniterapids__=1' in definitions or (
                          ('__znver4__=1' in definitions or '__znver5__=1' in definitions) and
                          '__AVX512VP2INTERSECT__=1' in definitions) else 0)
        native = '__graniterapids__=1' in definitions or ('__ARM_NEON=1' in definitions
                  and ('FINDHAP_NATIVE_NEOVERSE_V2=1' in definitions or 'FINDHAP_NATIVE_GB10=1' in definitions)) or (
                  ('__znver4__=1' in definitions or '__znver5__=1' in definitions)
                  and '__AVX512VP2INTERSECT__=1' in definitions)
        expected.update(FINDHAP_INHERIT_RECORD_OUTPUT=int(native),
                        FINDHAP_OUTPUT_STREAMS=2 if native else 0,
                        FINDHAP_OUTPUT_BATCH_WRITE=int(native),
                        FINDHAP_TURIN_BINDING=int((('__znver4__=1' in definitions or '__znver5__=1' in definitions)
                            and '__AVX512VP2INTERSECT__=1' in definitions) or (
                            ('__znver3__=1' in definitions or '__cascadelake__=1' in definitions)
                            and '__AVX2__=1' in definitions)))
        for definition in definitions:
            key,value=definition.split('=',1)
            if key in expected:expected[key]=int(value)
        actual = {key: int(policy.get(policy[key], policy[key])) for key in expected}
        assert actual == expected, (name, actual, expected)
        records.append(dict(case=name, definitions=definitions, selected=actual))
    if a.record:
        a.record.write_text(json.dumps(records, indent=2) + '\n')
    print(f'PASS: {len(cases)} native profiles/overrides; modern output Granite/V2/GB10/Turin, binding Turin/Milan/Cascade Lake')


if __name__ == '__main__':
    main()
