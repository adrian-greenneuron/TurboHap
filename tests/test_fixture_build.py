"""Regression checks for fixture profile propagation and single-runtime linking."""
from pathlib import Path
import unittest
import subprocess
import tempfile

from fixture_build import configured_settings

from fixture_build import compile_and_link_commands


class FixtureBuildTests(unittest.TestCase):
    def test_gnu_alternate_runtime_is_compilation_only(self):
        settings = dict(compiler='g++-16', common_flags='-fopenmp',
                        configuration_flags='-O3 -march=native -flto -ffp-contract=off',
                        compile_definitions=['FINDHAP_NATIVE_APPLE_M4PRO=1',
                                             'FINDHAP_HAVE_KMP_BLOCKTIME=1'],
                        openmp_compile_flags='-fopenmp',
                        openmp_include_dirs=['/prefix with spaces/include'],
                        openmp_libraries=['/prefix with spaces/lib/libomp.dylib'],
                        executable_link_flags='-Wl,-rpath,/opt/lib')
        commands = compile_and_link_commands(['launcher.cpp', 'stub.cpp'], 'fixture', '/tmp', settings)
        for command in commands[:-1]:
            self.assertIn('-fopenmp', command)
            self.assertIn('-DFINDHAP_NATIVE_APPLE_M4PRO=1', command)
            self.assertIn('-DFINDHAP_HAVE_KMP_BLOCKTIME=1', command)
            self.assertIn('-I/prefix with spaces/include', command)
            self.assertIn('-c', command)
        link = commands[-1]
        self.assertNotIn('-fopenmp', link)
        self.assertFalse(any('gomp' in value for value in link))
        self.assertEqual(link.count('/prefix with spaces/lib/libomp.dylib'), 1)
        self.assertIn('-flto', link)
        self.assertIn('-Wl,-rpath,/opt/lib', link)
        self.assertEqual(sum(value.endswith('.o') for value in link), 2)

    def test_gnu_driver_does_not_add_implicit_libgomp(self):
        compiler = configured_settings()['compiler']
        version = subprocess.check_output([compiler, '--version'], text=True)
        if 'Free Software Foundation' not in version:
            self.skipTest('GNU driver regression; another compiler is configured')
        with tempfile.TemporaryDirectory(prefix='turbohap-link-plan-') as directory:
            settings = dict(compiler=compiler, common_flags='-fopenmp',
                            openmp_compile_flags='-fopenmp',
                            openmp_libraries=[str(Path(directory)/'libomp.dylib')])
            commands = compile_and_link_commands(['fixture.cpp'], 'fixture', directory, settings)
            # -### prints the actual linker command without requiring the Mac
            # library, invoking the linker, or creating a linked executable.
            result = subprocess.run([*commands[-1], '-###'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('libomp.dylib', result.stderr)
            self.assertNotIn('-lgomp', result.stderr)
            self.assertNotIn('libgomp', result.stderr)

    def test_gnu_selected_runtime_links_explicitly(self):
        settings = dict(compiler='g++-13', openmp_compile_flags='-fopenmp',
                        openmp_libraries=['/usr/lib/libgomp.so', '/usr/lib/libpthread.a'])
        commands = compile_and_link_commands(['fixture.cpp'], 'fixture', '/tmp', settings)
        self.assertIn('-fopenmp', commands[0])
        self.assertNotIn('-fopenmp', commands[-1])
        self.assertIn('/usr/lib/libgomp.so', commands[-1])

    def test_direct_invocation_retains_default_runtime(self):
        settings = dict(compiler='c++', openmp_compile_flags='-fopenmp', openmp_libraries=[])
        commands = compile_and_link_commands(['fixture.cpp'], 'fixture', '/tmp', settings)
        self.assertIn('-fopenmp', commands[-1])

    def test_fixture_overrides_follow_profile_definitions(self):
        settings = dict(compiler='c++', compile_definitions=['FINDHAP_TURIN_BINDING=1'])
        command = compile_and_link_commands(['fixture.cpp'], 'fixture', '/tmp', settings,
                                             ['-DFINDHAP_TURIN_BINDING=0'])[0]
        self.assertLess(command.index('-DFINDHAP_TURIN_BINDING=1'),
                        command.index('-DFINDHAP_TURIN_BINDING=0'))


if __name__ == '__main__':
    unittest.main()
