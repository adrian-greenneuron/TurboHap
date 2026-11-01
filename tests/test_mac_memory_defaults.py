#!/usr/bin/env python3
"""Check native Apple Silicon phase-buffer defaults without running either engine.

CMake script fixtures simulate the post-OpenMP qualification inputs. The C++
fixtures preprocess the real phase-buffer header with the native compiler;
they never impersonate an Apple target or replace system/standard headers.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / 'cmake/PhaseBufferDefaults.cmake'
HEADER = ROOT / 'src/phase_buffer.hpp'


def bracket(value):
    assert ']==]' not in str(value)
    return '[==[' + str(value) + ']==]'


def cmake_cases():
    cases = []

    def case(name, apple=True, processor='arm64', cross=False, architectures='',
             compiler='GNU', runtime='omp', cpu_profile=True, brand='Apple M4 Pro',
             outer=None, cache=False, partition=None, repeat=False):
        # Expected policy deliberately ignores CPU brand, the independent M4
        # instruction profile, compiler family and OpenMP runtime selection.
        eligible = (apple and not cross and processor in ('arm64', 'aarch64')
                    and architectures in ('', 'arm64'))
        enabled = eligible if outer is None else outer
        active = eligible and enabled
        expected = {'FINDHAP_NATIVE_APPLE_SILICON_MEMORY': 'TRUE' if eligible else 'FALSE',
                    'FINDHAP_MMAP_PHASE_BUFFERS': 'ON' if enabled else 'OFF',
                    'FINDHAP_MAC_MEMORY_DEFAULTS': 'TRUE' if active else 'FALSE',
                    'FINDHAP_GNU_LTO_PARTITION': partition or ('one' if active else '1to1')}
        setup = ['cmake_minimum_required(VERSION 3.16)',
                 f'set(APPLE {"TRUE" if apple else "FALSE"})',
                 f'set(CMAKE_SYSTEM_PROCESSOR {bracket(processor)})',
                 f'set(CMAKE_CROSSCOMPILING {"TRUE" if cross else "FALSE"})',
                 f'set(CMAKE_OSX_ARCHITECTURES {bracket(architectures)})',
                 f'set(FINDHAP_QUALIFIED_APPLE_M4 {"TRUE" if cpu_profile else "FALSE"})',
                 f'set(FINDHAP_APPLE_OPTIMISATIONS {"ON" if cpu_profile else "OFF"})',
                 f'set(findhap_host_cpu {bracket(brand)})',
                 f'set(CMAKE_CXX_COMPILER_ID {bracket(compiler)})',
                 f'set(OpenMP_CXX_LIB_NAMES {bracket(runtime)})']
        if outer is not None:
            suffix = ' CACHE BOOL "Test explicit cache"' if cache else ''
            setup.append(f'set(FINDHAP_MMAP_PHASE_BUFFERS {"ON" if outer else "OFF"}{suffix})')
        if partition is not None:
            setup.append(f'set(FINDHAP_GNU_LTO_PARTITION {bracket(partition)} CACHE STRING "Test explicit LTO")')
        setup.append(f'include({bracket(MODULE)})')
        if repeat:
            # A cached OFF must survive repeat inclusion, rather than being
            # replaced by the eligible default. This also tests idempotence.
            setup.append(f'include({bracket(MODULE)})')
        for key, value in expected.items():
            if key == 'FINDHAP_GNU_LTO_PARTITION':
                condition = f'NOT "${{{key}}}" STREQUAL {bracket(value)}'
            else:
                condition = f'NOT {key}' if value in ('TRUE', 'ON') else key
            setup.extend([f'if({condition})',
                          f'  message(FATAL_ERROR "{name}: unexpected {key}=${{{key}}}; expected {value}")',
                          'endif()'])
        cases.append((name, '\n'.join(setup) + '\n', expected))

    for model in ('Apple M1', 'Apple M2 Pro', 'Apple M3 Max', 'Apple M4 Pro', 'Apple M5'):
        case(model.replace(' ', '_') + '_native_default', brand=model,
             cpu_profile=(model == 'Apple M4 Pro'))
    case('future_unknown_Apple_Silicon', brand='Unseen Apple Silicon', cpu_profile=False)
    case('M4_CPU_profile_explicitly_OFF_memory_independent', cpu_profile=False)
    case('native_aarch64_spelling', processor='aarch64', cpu_profile=False)
    case('native_explicit_arm64_architecture', architectures='arm64')
    case('native_explicit_OFF', outer=False)
    case('native_cached_OFF', outer=False, cache=True, repeat=True)
    case('native_explicit_ON', outer=True)
    case('native_cached_ON', outer=True, cache=True, repeat=True)
    for compiler in ('GNU', 'Clang', 'AppleClang'):
        for runtime in ('omp', 'gomp', ''):
            case('native_' + compiler + '_' + (runtime or 'no_runtime'),
                 compiler=compiler, runtime=runtime, cpu_profile=False)
    case('native_runtime_list_does_not_control_memory', runtime='omp;gomp')
    case('Linux_ARM64_default_unchanged', apple=False, processor='aarch64', runtime='gomp')
    case('Linux_x86_default_unchanged', apple=False, processor='x86_64', runtime='gomp')
    case('Intel_Mac_default_unchanged', processor='x86_64', brand='Intel Xeon')
    case('cross_build_default_unchanged', cross=True)
    case('universal_build_default_unchanged', architectures='arm64;x86_64')
    case('reverse_universal_build_default_unchanged', architectures='x86_64;arm64')
    case('explicit_x86_target_default_unchanged', architectures='x86_64')
    case('Linux_explicit_ON_keeps_nonMac_subdefaults', apple=False, outer=True)
    case('Linux_cached_ON_keeps_nonMac_subdefaults', apple=False, outer=True, cache=True)
    case('native_force_old_LTO', partition='1to1')
    case('native_custom_LTO', partition='max')
    case('Linux_explicit_one_LTO', apple=False, partition='one')
    case('disabled_explicit_one_LTO', outer=False, partition='one')
    return cases


def header_cases():
    old = dict(FINDHAP_MAC_MEMORY_DEFAULTS=0, FINDHAP_MMAP_PHASE_BUFFERS=0,
               FINDHAP_PHASE_LM_NATIVE_CACHE=1, FINDHAP_PHASE_LM_RETAIN_CACHE=1,
               FINDHAP_PHASE_LM_RESERVE=1, FINDHAP_PHASE_LM_WORKER_REUSE=0,
               FINDHAP_PHASE_LM_WORKER_MAPPING=0, FINDHAP_PHASE_STD_COMPACT_WORKSPACE=0,
               FINDHAP_PHASE_STD_SNAPSHOT_REUSE=0)
    active = dict(old, FINDHAP_MAC_MEMORY_DEFAULTS=1, FINDHAP_MMAP_PHASE_BUFFERS=1,
                  FINDHAP_PHASE_LM_NATIVE_CACHE=0, FINDHAP_PHASE_LM_RETAIN_CACHE=0,
                  FINDHAP_PHASE_LM_WORKER_REUSE=1, FINDHAP_PHASE_STD_COMPACT_WORKSPACE=1)
    cases = []

    def case(name, definitions, expected):
        cases.append((name, definitions, expected))

    case('native_no_profile', {}, old)
    case('marker_only_requires_outer_flag', {'FINDHAP_MAC_MEMORY_DEFAULTS': 1},
         dict(old, FINDHAP_MAC_MEMORY_DEFAULTS=1))
    case('manual_outer_ON_keeps_existing_subdefaults', {'FINDHAP_MMAP_PHASE_BUFFERS': 1},
         dict(old, FINDHAP_MMAP_PHASE_BUFFERS=1))
    case('explicit_zero_marker', {'FINDHAP_MAC_MEMORY_DEFAULTS': 0, 'FINDHAP_MMAP_PHASE_BUFFERS': 1},
         dict(old, FINDHAP_MMAP_PHASE_BUFFERS=1))
    case('qualified_marker_outer_OFF', {'FINDHAP_MAC_MEMORY_DEFAULTS': 1, 'FINDHAP_MMAP_PHASE_BUFFERS': 0},
         dict(old, FINDHAP_MAC_MEMORY_DEFAULTS=1))
    qualified = {'FINDHAP_MAC_MEMORY_DEFAULTS': 1, 'FINDHAP_MMAP_PHASE_BUFFERS': 1}
    case('qualified_marker_outer_ON', qualified, active)
    for name, value in [('FINDHAP_PHASE_STD_COMPACT_WORKSPACE', 0),
                        ('FINDHAP_PHASE_LM_NATIVE_CACHE', 1),
                        ('FINDHAP_PHASE_LM_WORKER_REUSE', 0),
                        ('FINDHAP_PHASE_LM_RETAIN_CACHE', 1),
                        ('FINDHAP_PHASE_LM_RESERVE', 0),
                        ('FINDHAP_PHASE_STD_SNAPSHOT_REUSE', 1),
                        ('FINDHAP_PHASE_STD_SNAPSHOT_REUSE', 2)]:
        expected = dict(active, **{name: value})
        if name == 'FINDHAP_PHASE_LM_NATIVE_CACHE':
            expected['FINDHAP_PHASE_LM_RETAIN_CACHE'] = value
        case('qualified_override_' + name + '_' + str(value), dict(qualified, **{name: value}), expected)
    case('explicit_retain_independent_of_native',
         dict(qualified, FINDHAP_PHASE_LM_NATIVE_CACHE=1, FINDHAP_PHASE_LM_RETAIN_CACHE=0),
         dict(active, FINDHAP_PHASE_LM_NATIVE_CACHE=1, FINDHAP_PHASE_LM_RETAIN_CACHE=0))
    case('mapped_worker_override_with_reuse_disabled',
         dict(qualified, FINDHAP_PHASE_LM_WORKER_REUSE=0, FINDHAP_PHASE_LM_WORKER_MAPPING=1),
         dict(active, FINDHAP_PHASE_LM_WORKER_REUSE=0, FINDHAP_PHASE_LM_WORKER_MAPPING=1))
    case('manual_subdefaults_without_qualification',
         {'FINDHAP_MMAP_PHASE_BUFFERS': 1, 'FINDHAP_PHASE_STD_COMPACT_WORKSPACE': 1,
          'FINDHAP_PHASE_LM_NATIVE_CACHE': 0, 'FINDHAP_PHASE_LM_WORKER_REUSE': 1},
         dict(active, FINDHAP_MAC_MEMORY_DEFAULTS=0))
    return cases


def macro_number(macros, name, visited=()):
    if name in visited:
        raise AssertionError('Recursive macro: ' + name)
    value = macros[name].strip()
    if re.fullmatch(r'-?\d+', value):
        return int(value)
    if re.fullmatch(r'[A-Za-z_]\w*', value):
        return macro_number(macros, value, (*visited, name))
    raise AssertionError(f'Unexpected policy expression {name}={value}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='g++-13')
    parser.add_argument('--record', type=Path)
    args = parser.parse_args()
    records = {'cmake': [], 'header': []}
    assert MODULE.is_file(), str(MODULE)
    cmake = shutil.which('cmake')
    assert cmake, 'cmake is required'
    with tempfile.TemporaryDirectory(prefix='turbohap-mac-memory-defaults-') as temp:
        for number, (name, script, expected) in enumerate(cmake_cases()):
            fixture = Path(temp) / f'case-{number}.cmake'
            fixture.write_text(script)
            subprocess.run([cmake, '-P', str(fixture)], check=True, capture_output=True, text=True)
            records['cmake'].append({'name': name, 'expected': expected, 'passed': True})
        for name, definitions, expected in header_cases():
            command = [args.compiler, '-std=c++17', '-E', '-dM', '-x', 'c++', str(HEADER)]
            command += [f'-D{key}={value}' for key, value in definitions.items()]
            result = subprocess.run(command, check=True, capture_output=True, text=True)
            macros = {}
            for line in result.stdout.splitlines():
                match = re.match(r'^#define ([A-Za-z_]\w*) (.*)$', line)
                if match:
                    macros[match.group(1)] = match.group(2)
            actual = {key: macro_number(macros, key) for key in expected}
            assert actual == expected, f'{name}: {actual} != {expected}'
            records['header'].append({'name': name, 'definitions': definitions,
                                      'actual': actual, 'passed': True})
    records['compiler'] = args.compiler
    records['counts'] = {name: len(records[name]) for name in ('cmake', 'header')}
    records['all_passed'] = True
    if args.record:
        args.record.parent.mkdir(parents=True, exist_ok=True)
        args.record.write_text(json.dumps(records, indent=2) + '\n')
    print(f"Mac memory defaults: {records['counts']['cmake']} CMake cases and "
          f"{records['counts']['header']} native-header cases passed")


if __name__ == '__main__':
    main()
