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
    # Explicit expected profiles are deliberately independent of the C++ guards.
    # Derive only fixture inheritance here, not expected decisions from -D flags.
    portable = dict(
        TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT=0,
        FINDHAP_BUFFER_INHERIT=0, FINDHAP_LM_ORDERED_BYTES=0,
        FINDHAP_POP_ENCODE=0, FINDHAP_POP_FULL=0, FINDHAP_PEDIGREE_REMATCH=0,
        FINDHAP_LIBRARY_FORMAT=0, FINDHAP_INHERIT_RECORD_OUTPUT=0,
        FINDHAP_OUTPUT_STREAMS=0, FINDHAP_OUTPUT_BATCH_WRITE=0,
        FINDHAP_TURIN_BINDING=0, FINDHAP_PEDIGREE_ORDERED_FINAL=0,
        FINDHAP_HAPLIST_INIT=0, FINDHAP_ORDERED_LAYOUT=0,
        FINDHAP_IO_BUFFER_BYTES=0, FINDHAP_MAC_BLOCKTIME_MS=-1,
        FINDHAP_MAC_DYNAMIC_ROWS=0)
    arm = dict(portable, FINDHAP_BUFFER_INHERIT=1,
               FINDHAP_LM_ORDERED_BYTES=4194304)
    avx512 = dict(portable, FINDHAP_POP_ENCODE=3, FINDHAP_POP_FULL=2)
    legacy_gb10 = dict(arm, FINDHAP_POP_ENCODE=4, FINDHAP_PEDIGREE_REMATCH=2,
                       FINDHAP_LIBRARY_FORMAT=2, FINDHAP_INHERIT_RECORD_OUTPUT=1,
                       FINDHAP_OUTPUT_STREAMS=2, FINDHAP_OUTPUT_BATCH_WRITE=1)
    legacy_v2 = dict(legacy_gb10, FINDHAP_PEDIGREE_REMATCH=0)
    ice_lake = dict(avx512, FINDHAP_BUFFER_INHERIT=1,
                    FINDHAP_LM_ORDERED_BYTES=4194304)
    legacy_granite = dict(ice_lake, FINDHAP_LIBRARY_FORMAT=2,
                          FINDHAP_INHERIT_RECORD_OUTPUT=1,
                          FINDHAP_OUTPUT_STREAMS=2, FINDHAP_OUTPUT_BATCH_WRITE=1)
    legacy_turin = dict(legacy_granite, FINDHAP_TURIN_BINDING=1)
    milan = dict(portable, FINDHAP_POP_ENCODE=2, FINDHAP_TURIN_BINDING=1)
    cascade = dict(portable, FINDHAP_TURIN_BINDING=1)
    bundle = dict(TURBOHAP_TESTED_MAC_BUNDLE_DEFAULT=1,
                  FINDHAP_PEDIGREE_ORDERED_FINAL=1, FINDHAP_HAPLIST_INIT=3,
                  FINDHAP_ORDERED_LAYOUT=1, FINDHAP_IO_BUFFER_BYTES=65536)
    gb10 = dict(legacy_gb10, **bundle)
    gb10.update(FINDHAP_POP_ENCODE=5, FINDHAP_POP_FULL=2)
    v2 = dict(gb10, FINDHAP_PEDIGREE_REMATCH=0)
    granite = dict(legacy_granite, **bundle)
    turin = dict(legacy_turin, **bundle)
    apple = dict(arm, FINDHAP_POP_ENCODE=5, FINDHAP_POP_FULL=2,
                 FINDHAP_LIBRARY_FORMAT=2, FINDHAP_OUTPUT_BATCH_WRITE=1,
                 FINDHAP_PEDIGREE_ORDERED_FINAL=1, FINDHAP_HAPLIST_INIT=3,
                 FINDHAP_ORDERED_LAYOUT=1, FINDHAP_IO_BUFFER_BYTES=65536)
    avx = ['__AVX2__=1', '__AVX512VBMI__=1', '__AVX512BW__=1']
    linux_arm = ['__linux__=1', '__aarch64__=1', '__ARM_NEON=1']
    linux_x86 = ['__linux__=1', '__x86_64__=1']
    gb10_defs = linux_arm + ['FINDHAP_NATIVE_GB10=1']
    v2_defs = linux_arm + ['FINDHAP_NATIVE_NEOVERSE_V2=1']
    granite_defs = linux_x86 + avx + ['__graniterapids__=1']
    turin_defs = linux_x86 + avx + ['__znver4__=1', '__AVX512VP2INTERSECT__=1']
    apple_defs = ['__APPLE__=1', '__aarch64__=1', '__ARM_NEON=1',
                  'FINDHAP_NATIVE_APPLE_M4PRO=1']
    cases = []

    def case(name, definitions, expected, **overrides):
        cases.append((name, definitions, dict(expected, **overrides)))

    def without(definitions, macro):
        return [d for d in definitions if not d.startswith(macro + '=')]

    case('portable', [], portable)
    case('ARM_NEON', ['__ARM_NEON=1'], arm)
    case('native_Linux_ARM_without_profile', linux_arm, arm)
    case('qualified_Neoverse_V2', v2_defs, v2)
    case('qualified_GB10', gb10_defs, gb10)
    case('GB10_legacy_markers', ['__ARM_NEON=1', 'FINDHAP_NATIVE_GB10=1'], legacy_gb10)
    case('V2_legacy_markers', ['__ARM_NEON=1', 'FINDHAP_NATIVE_NEOVERSE_V2=1'], legacy_v2)
    for name, definitions, old_profile in (
            ('GB10', gb10_defs, legacy_gb10), ('V2', v2_defs, legacy_v2)):
        case(name + '_requires_Linux', without(definitions, '__linux__'), old_profile)
        case(name + '_requires_ARM64', without(definitions, '__aarch64__'), old_profile)
        case(name + '_wrong_arch', without(definitions, '__aarch64__') + ['__x86_64__=1'], old_profile)
        case(name + '_requires_NEON', without(definitions, '__ARM_NEON'), portable,
             FINDHAP_LIBRARY_FORMAT=2)
    case('GB10_marker_zero', linux_arm + ['FINDHAP_NATIVE_GB10=0'], legacy_gb10)
    case('V2_marker_zero', linux_arm + ['FINDHAP_NATIVE_NEOVERSE_V2=0'], legacy_v2)
    case('Ice_Lake', linux_x86 + avx + ['__icelake_server__=1'], ice_lake)
    case('Granite_Rapids', granite_defs, granite)
    case('Turin_GCC13', turin_defs, turin)
    case('Turin_znver5_profile', linux_x86 + avx +
         ['__znver5__=1', '__AVX512VP2INTERSECT__=1'], turin)
    for name, definitions, old_profile in (
            ('Granite', granite_defs, legacy_granite), ('Turin', turin_defs, legacy_turin)):
        case(name + '_requires_Linux', without(definitions, '__linux__'), old_profile)
        case(name + '_requires_x86_64', without(definitions, '__x86_64__'), old_profile)
        case(name + '_wrong_arch', without(definitions, '__x86_64__') + ['__aarch64__=1'], old_profile)
        case(name + '_requires_VBMI', without(definitions, '__AVX512VBMI__'), old_profile,
             FINDHAP_POP_ENCODE=0, FINDHAP_POP_FULL=0)
        case(name + '_requires_BW', without(definitions, '__AVX512BW__'), old_profile,
             FINDHAP_POP_ENCODE=0, FINDHAP_POP_FULL=0)
        case(name + '_does_not_require_AVX2_marker', without(definitions, '__AVX2__'),
             granite if name == 'Granite' else turin)
    case('Genoa_without_VP2INTERSECT', linux_x86 + avx + ['__znver4__=1'], avx512)
    case('znver5_without_required_profile', linux_x86 + avx + ['__znver5__=1'], avx512)
    case('generic_VP2_not_AMD_profile', linux_x86 + avx + ['__AVX512VP2INTERSECT__=1'], avx512)
    case('generic_x86_AVX512', linux_x86 + avx, avx512)
    case('Milan', linux_x86 + ['__znver3__=1', '__AVX2__=1'], milan)
    case('Milan_without_AVX2', linux_x86 + ['__znver3__=1'], portable)
    case('Milan_with_extra_VP2_flag', linux_x86 + avx +
         ['__znver3__=1', '__AVX512VP2INTERSECT__=1'], milan)
    case('Cascade_Lake', linux_x86 +
         ['__cascadelake__=1', '__AVX2__=1', '__AVX512BW__=1'], cascade)
    case('Cascade_Lake_without_AVX2', linux_x86 + ['__cascadelake__=1'], portable)

    # Disabling or customising one feature must not reset another feature.
    new_features = ['FINDHAP_HAPLIST_INIT', 'FINDHAP_PEDIGREE_ORDERED_FINAL',
                    'FINDHAP_ORDERED_LAYOUT', 'FINDHAP_IO_BUFFER_BYTES']
    disable = {key: 0 for key in new_features}
    custom = dict(FINDHAP_HAPLIST_INIT=1, FINDHAP_PEDIGREE_ORDERED_FINAL=2,
                  FINDHAP_ORDERED_LAYOUT=2, FINDHAP_IO_BUFFER_BYTES=131072)
    for name, definitions, expected in (
            ('GB10', gb10_defs, gb10), ('V2', v2_defs, v2),
            ('Granite', granite_defs, granite), ('Turin', turin_defs, turin)):
        case(name + '_explicit_new_feature_disable', definitions +
             [f'{key}=0' for key in new_features], expected, **disable)
        case(name + '_explicit_new_feature_custom', definitions +
             [f'{key}={value}' for key, value in custom.items()], expected, **custom)
        case(name + '_explicit_encoder_disable', definitions + ['FINDHAP_POP_ENCODE=0'],
             expected, FINDHAP_POP_ENCODE=0, FINDHAP_POP_FULL=0)
        case(name + '_explicit_full_signature_disable', definitions + ['FINDHAP_POP_FULL=0'],
             expected, FINDHAP_POP_FULL=0)
        case(name + '_explicit_full_signature_custom', definitions + ['FINDHAP_POP_FULL=1'],
             expected, FINDHAP_POP_FULL=1)
    case('GB10_explicit_old_NEON_encoder', gb10_defs + ['FINDHAP_POP_ENCODE=4'], gb10,
         FINDHAP_POP_ENCODE=4, FINDHAP_POP_FULL=0)
    case('Granite_explicit_AVX2_encoder', granite_defs + ['FINDHAP_POP_ENCODE=2'], granite,
         FINDHAP_POP_ENCODE=2, FINDHAP_POP_FULL=0)
    case('explicit_lookup_disable', granite_defs +
         ['FINDHAP_BUFFER_INHERIT=0', 'FINDHAP_LM_ORDERED_BYTES=0'], granite,
         FINDHAP_BUFFER_INHERIT=0, FINDHAP_LM_ORDERED_BYTES=0)
    case('explicit_binding_disable', turin_defs + ['FINDHAP_TURIN_BINDING=0'], turin,
         FINDHAP_TURIN_BINDING=0)
    case('explicit_Milan_binding_disable', ['__znver3__=1', '__AVX2__=1',
         'FINDHAP_TURIN_BINDING=0'], milan, FINDHAP_TURIN_BINDING=0)
    case('explicit_Cascade_binding_disable', ['__cascadelake__=1', '__AVX2__=1',
         'FINDHAP_TURIN_BINDING=0'], cascade, FINDHAP_TURIN_BINDING=0)
    case('explicit_custom_budget', ['FINDHAP_BUFFER_INHERIT=1',
         'FINDHAP_LM_ORDERED_BYTES=8192'], portable,
         FINDHAP_BUFFER_INHERIT=1, FINDHAP_LM_ORDERED_BYTES=8192)
    case('explicit_output_disable', granite_defs + ['FINDHAP_INHERIT_RECORD_OUTPUT=0',
         'FINDHAP_OUTPUT_STREAMS=0', 'FINDHAP_OUTPUT_BATCH_WRITE=0'], granite,
         FINDHAP_INHERIT_RECORD_OUTPUT=0, FINDHAP_OUTPUT_STREAMS=0,
         FINDHAP_OUTPUT_BATCH_WRITE=0)
    case('explicit_library_disable', gb10_defs + ['FINDHAP_LIBRARY_FORMAT=0'], gb10,
         FINDHAP_LIBRARY_FORMAT=0)
    case('explicit_library_candidate', ['FINDHAP_LIBRARY_FORMAT=2'], portable,
         FINDHAP_LIBRARY_FORMAT=2)
    case('explicit_rematch_disable', gb10_defs + ['FINDHAP_PEDIGREE_REMATCH=0'], gb10,
         FINDHAP_PEDIGREE_REMATCH=0)
    case('explicit_rematch_candidate', ['FINDHAP_PEDIGREE_REMATCH=2'], portable,
         FINDHAP_PEDIGREE_REMATCH=2)
    case('Apple_M4_Pro', apple_defs, apple)
    for name, macro in (('Darwin', '__APPLE__'), ('ARM64', '__aarch64__')):
        case('Apple_requires_' + name, without(apple_defs, macro), arm)
    case('Apple_requires_NEON', without(apple_defs, '__ARM_NEON'), portable)
    case('Apple_marker_zero', without(apple_defs, 'FINDHAP_NATIVE_APPLE_M4PRO') +
         ['FINDHAP_NATIVE_APPLE_M4PRO=0'], arm)
    apple_disable = dict(disable, FINDHAP_LIBRARY_FORMAT=0, FINDHAP_OUTPUT_BATCH_WRITE=0,
                         FINDHAP_POP_ENCODE=0, FINDHAP_POP_FULL=0)
    case('Apple_explicit_disable', apple_defs +
         [f'{key}={value}' for key, value in apple_disable.items()], apple, **apple_disable)
    # Preserve the existing Apple profile, including its independent FULL default.
    case('Apple_existing_encoder_override', apple_defs + ['FINDHAP_POP_ENCODE=0'], apple,
         FINDHAP_POP_ENCODE=0)

    records = []
    for name, definitions, expected in cases:
        policy = preprocess(a.compiler, header, definitions)
        actual = {key: int(policy.get(policy[key], policy[key])) for key in expected}
        assert actual == expected, (name, actual, expected)
        records.append(dict(case=name, definitions=definitions, selected=actual))
    # This exact closing scan follows the architecture independently of the
    # earlier CPU-specific bundles and the workload-profile opt-in.
    final_scan_cases = [
        ('final_scan_portable', [], 0),
        ('final_scan_x86', linux_x86 + avx, 0),
        ('final_scan_ARM32_NEON', ['__arm__=1', '__ARM_NEON=1'], 0),
        ('final_scan_NEON_without_ARM64', ['__ARM_NEON=1'], 0),
        ('final_scan_ARM64_without_NEON', ['__aarch64__=1'], 0),
        ('final_scan_generic_ARM64_NEON', ['__aarch64__=1', '__ARM_NEON=1'], 1),
        ('final_scan_ARM64_alias', ['__arm64__=1', '__ARM_NEON=1'], 1),
        ('final_scan_Spark', gb10_defs, 1),
        ('final_scan_ARM_C4A', v2_defs, 1),
        ('final_scan_Apple_without_model',
         ['__APPLE__=1', '__aarch64__=1', '__ARM_NEON=1'], 1),
        ('final_scan_Apple_M4_Pro', apple_defs, 1),
        ('final_scan_no_workload_profile', linux_arm + ['FINDHAP_WORKLOAD_OPTIMISATIONS=0'], 1),
        ('final_scan_Spark_explicit_disable', gb10_defs + ['FINDHAP_POP_FINAL_SCAN_NEON=0'], 0),
        ('final_scan_C4A_explicit_disable', v2_defs + ['FINDHAP_POP_FINAL_SCAN_NEON=0'], 0),
        ('final_scan_Apple_explicit_disable', apple_defs + ['FINDHAP_POP_FINAL_SCAN_NEON=0'], 0),
        ('final_scan_explicit_enable', linux_arm + ['FINDHAP_POP_FINAL_SCAN_NEON=1'], 1),
    ]
    for name, definitions, expected in final_scan_cases:
        policy = preprocess(a.compiler, header, definitions)
        actual = int(policy['FINDHAP_POP_FINAL_SCAN_NEON'])
        if actual != expected:
            raise RuntimeError(f'{name}: final scan {actual}, expected {expected}')
        records.append(dict(case=name, definitions=definitions,
                            selected=dict(FINDHAP_POP_FINAL_SCAN_NEON=actual)))
    if a.record:
        a.record.write_text(json.dumps(records, indent=2) + '\n')
    print(f'PASS: {len(cases)} native profiles/overrides and {len(final_scan_cases)} '
          'ARM64 final-scan policies; native NEON/AVX retained')


if __name__ == '__main__':
    main()
