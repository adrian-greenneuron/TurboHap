"""Build Python test fixtures with the configured production compiler/runtime.

Compile and link independently: GNU's -fopenmp enables lowering when compiling,
but would silently add libgomp if repeated while linking a selected libomp build.
"""
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def configured_settings():
    filename = os.environ.get('FINDHAP_TEST_BUILD_CONFIG')
    if filename:
        return json.loads(Path(filename).read_text())
    # Preserve direct invocation outside CTest, with the normal compiler runtime.
    return dict(compiler=os.environ.get('CXX', 'c++'), common_flags='',
                configuration_flags='', compile_definitions=[],
                openmp_compile_flags='-fopenmp', openmp_include_dirs=[],
                openmp_libraries=[], executable_link_flags='')


def compile_and_link_commands(sources, binary, directory, settings, extra_flags=()):
    compiler = settings['compiler']
    common = shlex.split(settings.get('common_flags', ''))
    common += shlex.split(settings.get('configuration_flags', ''))
    definitions = ['-D' + value for value in settings.get('compile_definitions', [])]
    includes = ['-I' + value for value in settings.get('openmp_include_dirs', [])]
    openmp = shlex.split(settings.get('openmp_compile_flags', '-fopenmp'))
    objects = [Path(directory) / ('fixture-' + str(i) + '.o') for i in range(len(sources))]
    commands = [[compiler, '-std=c++17', *common, '-O2', *definitions,
                 *includes, *openmp, *map(str, extra_flags), '-c', str(source), '-o', str(obj)]
                for source, obj in zip(sources, objects)]
    libraries = settings.get('openmp_libraries', [])
    # Ambient/common flags may themselves contain an OpenMP switch. Explicit
    # runtime linkage must still never ask the compiler to add another runtime.
    link_common = [flag for flag in common
                   if not (flag == '-fopenmp' or flag.startswith('-fopenmp='))]
    link_openmp = [] if libraries else openmp
    commands.append([compiler, *link_common,
                     *shlex.split(settings.get('executable_link_flags', '')),
                     *settings.get('link_options', []), *map(str, objects), *link_openmp, *libraries, '-o', str(binary)])
    return commands


def build_fixture(sources, binary, extra_flags=(), capture_output=True):
    with tempfile.TemporaryDirectory(prefix='turbohap-fixture-objects-') as directory:
        commands = compile_and_link_commands(sources, binary, directory,
                                             configured_settings(), extra_flags)
        for command in commands:
            subprocess.run(command, check=True, capture_output=capture_output)
