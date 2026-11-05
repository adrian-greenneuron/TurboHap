"""The release gate must reject bad evidence even under Python -O."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve()
CHECKER = HERE.parents[1] / 'tools/check_release.py'
if not CHECKER.exists():
    CHECKER = HERE.parents[3] / 'tools/public_release/check_release.py'
sys.path.insert(0, str(CHECKER.parent))
spec = importlib.util.spec_from_file_location('release_checker', CHECKER)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def valid_evidence():
    datasets = list(checker.benchmark_datasets.__globals__['RELEASE_DATASETS'])
    repeats = {'Fortran baseline': 1, 'TurboHap': 2, 'TurboHap LM': 2}
    data = dict(release_tag='v1.3.0', repeats=repeats, observations=[],
                dataset_selection=dict(minimum_loci=10000, datasets=datasets))
    checked = dict(version='1.3.0', checks=[], fortran_checks=[],
                   strict_raw_output_checks=200, fortran_exact_records_passed=True,
                   fortran_raw_identical_files=45, fortran_raw_files_checked=50)
    outputs = {str(i): dict(bytes=i, sha256='0' * 64) for i in range(9)}
    outputs['cross.overs'] = dict(bytes=10, sha256='0' * 64)
    for dataset in datasets:
        for program, count in repeats.items():
            for repeat in range(1, count + 1):
                key = dict(dataset=dataset, program=program, repeat=repeat)
                data['observations'].append(dict(**key, wall_seconds=1, peak_rss_kib=1,
                                                swaps=0, output_check='pass'))
                if program != 'Fortran baseline':
                    checked['checks'].append(dict(**key, raw_passed=True, outputs=copy.deepcopy(outputs)))
                else:
                    files = {name: dict(passed=True, raw_match=name != 'cross.overs') for name in outputs}
                    files['cross.overs'].update(canonical_match=True,
                        canonical=dict(final_newline=True, header_hex='0a'))
                    checked['fortran_checks'].append(dict(**key, passed=True,
                        exact_record_equivalence_passed=True, raw_passed=False,
                        raw_mismatches=['cross.overs'], files=files, outputs=copy.deepcopy(outputs)))
    return data, checked


class ReleaseValidationTests(unittest.TestCase):
    def public_fixture(self, root):
        (root / 'src').mkdir()
        (root / 'CMakeLists.txt').write_text('project(TurboHap VERSION 1.4.0 LANGUAGES CXX)\n')
        (root / 'src/findhap.cpp').write_text('int main() { return 0; }\n')
        (root / 'SHA256SUMS').write_text('fixture\n')
        files = {'CMakeLists.txt': 'build', 'src/findhap.cpp': 'production-source',
                 'SHA256SUMS': 'licence-metadata', 'PUBLIC_PACKAGE_FILES.json': 'release-tooling'}
        (root / 'PUBLIC_PACKAGE_FILES.json').write_text(json.dumps(
            dict(schema_version=1, release_version='1.4.0', files=files)))
        return sorted(set(files) - {'SHA256SUMS'})

    def test_clean_public_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checker.check_public_boundary(root, self.public_fixture(root), '1.4.0')

    def test_public_boundary_rejects_development_and_untracked_files(self):
        for name in ('experimental/README.md', 'variants/simd.patch',
                     'src/experimental_input_validation.hpp', 'private.txt'):
            with self.subTest(path=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                names = self.public_fixture(root)
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('must remain outside the public package\n')
                with self.assertRaises(ValueError):
                    checker.check_public_boundary(root, names, '1.4.0')

    def test_public_boundary_rejects_even_allowlisted_experiments(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = self.public_fixture(root)
            name = 'experimental/README.md'
            path = root / name
            path.parent.mkdir()
            path.write_text('disabled experiment\n')
            policy = json.loads((root / 'PUBLIC_PACKAGE_FILES.json').read_text())
            policy['files'][name] = 'user-documentation'
            (root / 'PUBLIC_PACKAGE_FILES.json').write_text(json.dumps(policy))
            with self.assertRaises(ValueError):
                checker.check_public_boundary(root, names + [name], '1.4.0')

    def test_public_boundary_rejects_disabled_build_hooks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = self.public_fixture(root)
            (root / 'CMakeLists.txt').write_text('option(FINDHAP_EXPERIMENTAL_SAFETY "disabled" OFF)\n')
            with self.assertRaises(ValueError):
                checker.check_public_boundary(root, names, '1.4.0')

    def test_explicit_evidence_selection(self):
        suffix, generated = checker.select_evidence('turbohap_benchmarks_041126')
        self.assertEqual(suffix, '041126')
        self.assertIn('docs/turbohap_benchmarks_041126.csv', generated)
        for bad in ('../turbohap_benchmarks_041126', 'turbohap_benchmarks_321126', 'latest'):
            with self.assertRaises(ValueError):
                checker.select_evidence(bad)

    def test_rebuilt_executable_identity(self):
        identity = dict(measured_sha256='a' * 64, rebuilt_sha256='a' * 64)
        data = dict(measured_binary_sha256={'TurboHap': 'a' * 64, 'TurboHap LM': 'a' * 64},
                    release_build_validation=dict(binary_sha256='a' * 64,
                        binary_identity_verification=dict(method='rebuilt-production-source',
                            **{'findhap': identity.copy(), 'turbohap-validate-inputs': identity.copy()})))
        checker.check_build_identity(data)
        for target in ('findhap', 'turbohap-validate-inputs'):
            changed = copy.deepcopy(data)
            changed['release_build_validation']['binary_identity_verification'][target]['rebuilt_sha256'] = 'b' * 64
            with self.assertRaises(ValueError):
                checker.check_build_identity(changed)
        changed = copy.deepcopy(data)
        changed['measured_binary_sha256']['TurboHap LM'] = 'b' * 64
        with self.assertRaises(ValueError):
            checker.check_build_identity(changed)

    def test_valid_evidence(self):
        self.assertEqual(checker.check_validation(*valid_evidence()), 20)

    def test_invalid_evidence(self):
        def failed_raw(data, checked): checked['checks'][0]['raw_passed'] = False
        def invalid_sha(data, checked): checked['checks'][0]['outputs']['0']['sha256'] = 'bad'
        def missing_run(data, checked): data['observations'].pop()
        def duplicate_run(data, checked): data['observations'].append(data['observations'][0])
        def differing_output(data, checked): checked['checks'][0]['outputs']['0']['bytes'] += 1
        def negative_time(data, checked): data['observations'][0]['wall_seconds'] = -1
        def bad_scope(data, checked): data['dataset_selection']['minimum_loci'] = 1
        def extra_exception(data, checked): checked['fortran_checks'][0]['raw_mismatches'].append('hap.inherit')
        for mutation in (failed_raw, invalid_sha, missing_run, duplicate_run, differing_output,
                         negative_time, bad_scope, extra_exception):
            with self.subTest(mutation=mutation.__name__):
                data, checked = valid_evidence()
                mutation(data, checked)
                with self.assertRaises(ValueError):
                    checker.check_validation(data, checked)


if __name__ == '__main__':
    if '--child' in sys.argv:
        sys.argv.remove('--child')
        unittest.main()
    else:
        for flags in ([], ['-O']):
            subprocess.run([sys.executable, *flags, str(HERE), '--child'], check=True)
