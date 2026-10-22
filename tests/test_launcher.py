"""Exercise startup dispatch without processing a dataset."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

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
''')
        cls.binary = cls.root / 'findhap'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O2',
                        str(ROOT/'src/launcher.cpp'), str(stub), '-o', str(cls.binary)],
                       check=True, capture_output=True)

    def check(self, args, mode, forwarded):
        result = subprocess.run([str(self.binary), *args], cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 21 if mode == 'standard' else 22)
        self.assertEqual(result.stdout.splitlines(), [mode, *forwarded])
        self.assertEqual(result.stderr, '')

    def test_default_and_legacy_arguments(self):
        for args in ([], ['10'], ['010'], ['100'], ['8', 'R', 'ignored'], ['0'], ['bad']):
            with self.subTest(args=args):
                self.check(args, 'standard', args)

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

    def test_only_exact_flag_is_consumed(self):
        self.check(['10', '--low-memory-extra', '--help'], 'standard',
                   ['10', '--low-memory-extra', '--help'])


if __name__ == '__main__':
    unittest.main()
