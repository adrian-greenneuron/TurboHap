"""Exercise startup dispatch without processing a dataset."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from fixture_build import build_fixture

ROOT = Path(__file__).resolve().parents[1]


class LauncherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        stub = cls.root / 'engines.cpp'
        stub.write_text(r'''
#include <cstdio>
static int check(const char* mode, int code, int argc, char* argv[]) {
    if (argv[argc] != nullptr) return 99;
    std::puts(mode);
    for (int i = 1; i < argc; ++i) std::puts(argv[i]);
    return code;
}
namespace findhap_standard {
int run(int argc, char* argv[]) { return check("standard", 21, argc, argv); }
}
namespace findhap_low_memory {
int run(int argc, char* argv[]) { return check("low-memory", 22, argc, argv); }
}
namespace findhap_dynamic_standard {
int run(int argc, char* argv[]) { return check("dynamic-standard", 23, argc, argv); }
}
namespace findhap_dynamic_low_memory {
int run(int argc, char* argv[]) { return check("dynamic-low-memory", 24, argc, argv); }
}
''')
        cls.binary = cls.root / 'findhap'
        build_fixture([ROOT/'src/launcher.cpp', stub], cls.binary)

    def check(self, args, mode, forwarded):
        result = subprocess.run([str(self.binary), *args], cwd=self.root, capture_output=True, text=True)
        actual=result.stdout.splitlines()[0]
        self.assertIn(actual, [mode, "dynamic-"+mode])
        codes={'standard':21,'low-memory':22,'dynamic-standard':23,'dynamic-low-memory':24}
        self.assertEqual(result.returncode, codes[actual])
        self.assertEqual(result.stdout.splitlines(), [actual, *forwarded])
        self.assertIn('TurboHap CPU policy=', result.stderr)

    def test_default_and_legacy_arguments(self):
        for args in ([], ['10'], ['010'], ['100'], ['8', 'R', 'ignored']):
            with self.subTest(args=args):
                self.check(args, 'standard', args)

    def test_invalid_threads_and_policy_fail_before_engine(self):
        for args in (['0'], ['bad'], ['10', '--cpu-policy=fast'], ['--cpu-policy'],
                     ['10', '--cpu-policy=auto', '--cpu-policy=inherit']):
            result = subprocess.run([str(self.binary), *args], cwd=self.root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, '')
            self.assertIn('TurboHap:', result.stderr)

    def test_policy_positions_and_dry_run(self):
        for args in (['--cpu-policy=inherit', '10'], ['10', '--cpu-policy=inherit']):
            self.check(args, 'standard', ['10'])
        result = subprocess.run([str(self.binary), '10', '--cpu-info'], cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0)
        self.assertIn('"dry_run":true', result.stdout)
        self.assertEqual(result.stderr, '')

    def test_flag_positions_and_repetition(self):
        for args, forwarded in (
            (['--low-memory'], []),
            (['10', '--low-memory'], ['10']),
            (['--low-memory', '10'], ['10']),
            (['8', 'R', '--low-memory', 'ignored'], ['8', 'R', 'ignored']),
            (['--low-memory', '--low-memory', '010'], ['010']),
        ):
            with self.subTest(args=args):
                self.check(args, 'low-memory', forwarded)

    def test_help_needs_no_inputs_or_engine(self):
        for args in (['--help'], ['--low-memory', '--help'], ['--help', '--low-memory']):
            result = subprocess.run([str(self.binary), *args], cwd=self.root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0)
            self.assertIn('Usage: findhap [threads] [--low-memory]', result.stdout)

    def test_row_schedule_overrides_and_validation(self):
        for mode, flags in [('standard', []), ('low-memory', ['--low-memory'])]:
            for policy in ['static','dynamic']:
                args=['--row-schedule='+policy,'11',*flags]
                result=subprocess.run([str(self.binary),*args],cwd=self.root,capture_output=True,text=True)
                expected=mode if policy=='static' else 'dynamic-'+mode
                self.assertEqual(result.stdout.splitlines(),[expected,'11'])
                self.assertIn('TurboHap row schedule='+('static' if policy=='static' else 'dynamic,4'),result.stderr)
        for args in [['--row-schedule=bad'],['--row-schedule'],['--row-schedule=static','--row-schedule=dynamic']]:
            result=subprocess.run([str(self.binary),*args],cwd=self.root,capture_output=True,text=True)
            self.assertEqual(result.returncode,2)
            self.assertEqual(result.stdout,'')

    def test_only_exact_flag_is_consumed(self):
        self.check(['10', '--low-memory-extra', '--help'], 'standard',
                   ['10', '--low-memory-extra', '--help'])

    def test_input_validation_flags(self):
        for value in ('strict', 'warn'):
            for args in ([f'--input-validation={value}', '10'],
                         ['10', f'--input-validation={value}']):
                self.check(args, 'standard', ['10'])
        for args in (['--input-validation'], ['--input-validation=off'],
                     ['--input-validation=strict', '--input-validation=warn']):
            result = subprocess.run([str(self.binary), *args], cwd=self.root,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, '')
            self.assertIn('input-validation', result.stderr)


if __name__ == '__main__':
    unittest.main()
