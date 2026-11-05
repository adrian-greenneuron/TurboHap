#!/usr/bin/env python3
"""Check the complete public package, including generated claims and file hashes."""
import argparse
from collections import Counter
from datetime import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import unquote, urlsplit

from release_summary import benchmark_datasets, release_suffix

ROOT = Path(__file__).resolve().parents[1]
BASE = 'turbohap_benchmarks_011126'
SUFFIX = BASE.rsplit('_', 1)[1]
GENERATED = ['README.md', f'docs/{BASE}.csv', f'docs/{BASE}.md',
             f'docs/turbohap_release_claims_{SUFFIX}.json',
             f'docs/turbohap_release_notes_{SUFFIX}.md']
GENERATED += ['docs/assets/' + BASE + suffix + '.' + ext
              for suffix in ['', '_speedup', '_memory'] for ext in ['png', 'svg']]


def select_evidence(base):
    """An evidence date is an explicit input, never the most recent filename."""
    if not re.fullmatch(r'turbohap_benchmarks_\d{6}', base):
        raise ValueError('Use an evidence basename: turbohap_benchmarks_DDMMYY')
    suffix = base.rsplit('_', 1)[1]
    datetime.strptime(suffix, '%d%m%y')
    generated = ['README.md', f'docs/{base}.csv', f'docs/{base}.md',
                 f'docs/turbohap_release_claims_{suffix}.json',
                 f'docs/turbohap_release_notes_{suffix}.md']
    generated += ['docs/assets/' + base + variant + '.' + ext
                  for variant in ['', '_speedup', '_memory'] for ext in ['png', 'svg']]
    return suffix, generated


def digest(path):
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def package_files():
    if (ROOT / '.git').exists():
        names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')
        return sorted(n for n in names if n and n != 'SHA256SUMS')
    return sorted(str(p.relative_to(ROOT)) for p in ROOT.rglob('*')
                  if p.is_file() and p != ROOT / 'SHA256SUMS' and '__pycache__' not in p.parts)


PUBLIC_ROLES = {'production-source', 'build', 'regression-test', 'example',
                'user-documentation', 'benchmark-evidence', 'historical-evidence',
                'frozen-manuscript', 'release-tooling', 'licence-metadata'}
PUBLIC_TOP_FILES = {'CMakeLists.txt', 'README.md', 'LICENSE', 'MACOS.md',
                    'CHANGELOG.md', 'CITATION.cff', 'SHA256SUMS',
                    '.gitattributes', '.gitignore', 'PUBLIC_PACKAGE_FILES.json'}
PUBLIC_TOP_DIRS = {'src', 'cmake', 'tests', 'examples', 'docs', 'tools'}
FORBIDDEN_PUBLIC_FILES = {
    'src/experimental_input_validation.hpp', 'src/resource_safety.hpp',
    'src/worker_errors.hpp', 'tests/test_sampled_validation.cpp',
    'tests/test_resource_safety.cpp', 'tests/test_worker_failure.py',
    'tests/allocation_fault.cpp', 'tests/benchmark_conflict_scan.cpp',
}


def check_public_boundary(root, names, version):
    """A committed tree and matching hashes do not qualify development material."""
    policy = json.loads((root / 'PUBLIC_PACKAGE_FILES.json').read_text())
    if policy.get('schema_version') != 1 or policy.get('release_version') != version:
        raise ValueError('Public file allowlist schema/version differs')
    approved = policy['files']
    if not isinstance(approved, dict) or any(role not in PUBLIC_ROLES for role in approved.values()):
        raise ValueError('Every public file needs a reviewed distribution role')
    expected = set(names) | {'SHA256SUMS'}
    if set(approved) != expected:
        raise ValueError('Public package differs from its reviewed per-file allowlist')
    actual = set()
    for path in root.rglob('*'):
        relative = path.relative_to(root)
        if '.git' in relative.parts or '__pycache__' in relative.parts:
            continue
        if path.is_symlink():
            raise ValueError('Public package contains a symlink: ' + str(relative))
        if path.is_file():
            actual.add(relative.as_posix())
    if actual != expected:
        raise ValueError('Unreviewed or missing working-directory files in public package')
    for name in expected:
        path = Path(name)
        if path.is_absolute() or '..' in path.parts:
            raise ValueError('Unsafe public file path')
        if (len(path.parts) == 1 and name not in PUBLIC_TOP_FILES
                or len(path.parts) > 1 and path.parts[0] not in PUBLIC_TOP_DIRS
                or name in FORBIDDEN_PUBLIC_FILES or path.suffix == '.patch'):
            raise ValueError('Development-only or unsupported public file: ' + name)
    for name in expected:
        if name == 'CMakeLists.txt' or name.startswith(('src/', 'cmake/')):
            text = (root / name).read_text()
            if 'FINDHAP_EXPERIMENTAL_' in text or 'FINDHAP_FEATURE_DEFINITIONS' in text or 'experimental/' in text:
                raise ValueError('Unpromoted build/source hook in public package: ' + name)


def observation_key(row):
    return row['dataset'], row['program'], row['repeat']


def check_build_identity(data):
    """Bind reused timing evidence to the actual rebuilt production executables."""
    build = data['release_build_validation']
    measured = data['measured_binary_sha256']
    binary = build['binary_sha256']
    if not re.fullmatch('[0-9a-f]{64}', binary):
        raise ValueError('Invalid qualified executable hash')
    if any(measured[program] != binary for program in ('TurboHap', 'TurboHap LM')):
        raise ValueError('Packaged inference differs from the measured executable')
    if build.get('metadata_only_change_from_measured_source') is True:
        return
    verification = build['binary_identity_verification']
    if verification.get('method') != 'rebuilt-production-source':
        raise ValueError('Production rebuild evidence is required')
    for name in ('findhap', 'turbohap-validate-inputs'):
        record = verification[name]
        if (not re.fullmatch('[0-9a-f]{64}', record['measured_sha256'])
                or record['measured_sha256'] != record['rebuilt_sha256']):
            raise ValueError('Production rebuild changed executable bytes: ' + name)
    if verification['findhap']['rebuilt_sha256'] != binary:
        raise ValueError('Rebuild identity differs from qualification identity')


def check_validation(data, checked):
    """Join every public timed observation to its retained output evidence."""
    if not (checked['version'] == data['release_tag'].removeprefix('v')):
        raise ValueError("Validation failed: checked['version'] == data['release_tag'].removeprefix('v')")
    observations = data['observations']
    datasets = set(benchmark_datasets(data))
    if not (data['repeats'] == {'Fortran baseline': 1, 'TurboHap': 2, 'TurboHap LM': 2}):
        raise ValueError("Validation failed: data['repeats'] == {'Fortran baseline': 1, 'TurboHap': 2, 'TurboHap LM': 2}")
    expected = Counter((d, p, r) for d in datasets for p, n in data['repeats'].items()
                       for r in range(1, n + 1))
    if not (Counter(map(observation_key, observations)) == expected):
        raise ValueError('Missing or duplicate timed observations')
    if not (all((r['wall_seconds'] > 0 and r['peak_rss_kib'] > 0 and (r['swaps'] == 0) and r['output_check'].startswith('pass') for r in observations))):
        raise ValueError("Validation failed: all((r['wall_seconds'] > 0 and r['peak_rss_kib'] > 0 and (r['swaps'] == 0) and r['output_check'].startswith('pass') for r in observations))")
    cpp_expected = Counter({key: count for key, count in expected.items() if key[1] != 'Fortran baseline'})
    if not (Counter(map(observation_key, checked['checks'])) == cpp_expected):
        raise ValueError("Validation failed: Counter(map(observation_key, checked['checks'])) == cpp_expected")
    if not (all((r['raw_passed'] and len(r['outputs']) == 10 for r in checked['checks']))):
        raise ValueError("Validation failed: all((r['raw_passed'] and len(r['outputs']) == 10 for r in checked['checks']))")
    if not (checked['strict_raw_output_checks'] == sum((len(r['outputs']) for r in checked['checks'])) == 10 * len(cpp_expected)):
        raise ValueError("Validation failed: checked['strict_raw_output_checks'] == sum((len(r['outputs']) for r in checked['checks'])) == 10 * len(cpp_expected)")
    reference_outputs = {}
    for row in checked['checks']:
        outputs = row['outputs']
        for result in outputs.values():
            if not (type(result['bytes']) is int and result['bytes'] >= 0):
                raise ValueError("Validation failed: type(result['bytes']) is int and result['bytes'] >= 0")
            if not (re.fullmatch('[0-9a-f]{64}', result['sha256'])):
                raise ValueError("Validation failed: re.fullmatch('[0-9a-f]{64}', result['sha256'])")
        previous = reference_outputs.setdefault(row['dataset'], outputs)
        if not (outputs == previous):
            raise ValueError(f"Output identities differ across repeats/modes: {row['dataset']}")
    fortran = checked['fortran_checks']
    if not (Counter(map(observation_key, fortran)) == Counter({key: count for key, count in expected.items() if key[1] == 'Fortran baseline'})):
        raise ValueError("Validation failed: Counter(map(observation_key, fortran)) == Counter({key: count for key, count in expected.items() if key[1] == 'Fortran baseline'})")
    if not (checked['fortran_exact_records_passed']):
        raise ValueError("Validation failed: checked['fortran_exact_records_passed']")
    for row in fortran:
        if not (row['passed'] and row['exact_record_equivalence_passed']):
            raise ValueError("Validation failed: row['passed'] and row['exact_record_equivalence_passed']")
        if not (not row['raw_passed'] and row['raw_mismatches'] == ['cross.overs']):
            raise ValueError("Validation failed: not row['raw_passed'] and row['raw_mismatches'] == ['cross.overs']")
        if not (len(row['files']) == 10 and all((result['passed'] for result in row['files'].values()))):
            raise ValueError("Validation failed: len(row['files']) == 10 and all((result['passed'] for result in row['files'].values()))")
        if not ({name for name, result in row['files'].items() if not result['raw_match']} == {'cross.overs'}):
            raise ValueError("Validation failed: {name for name, result in row['files'].items() if not result['raw_match']} == {'cross.overs'}")
        if not (row['files']['cross.overs']['canonical_match']):
            raise ValueError("Validation failed: row['files']['cross.overs']['canonical_match']")
        if not (row['files']['cross.overs']['canonical']['final_newline']):
            raise ValueError("Validation failed: row['files']['cross.overs']['canonical']['final_newline']")
        if not (row['files']['cross.overs']['canonical']['header_hex']):
            raise ValueError("Validation failed: row['files']['cross.overs']['canonical']['header_hex']")
        if not (set(row['outputs']) == set(reference_outputs[row['dataset']])):
            raise ValueError("Validation failed: set(row['outputs']) == set(reference_outputs[row['dataset']])")
        for name, identity in row['outputs'].items():
            if name != 'cross.overs':
                if not (identity == reference_outputs[row['dataset']][name]):
                    raise ValueError("Validation failed: identity == reference_outputs[row['dataset']][name]")
    raw_identical = sum(result['raw_match'] for row in fortran for result in row['files'].values())
    raw_checked = sum(len(row['files']) for row in fortran)
    if not (checked['fortran_raw_identical_files'] == raw_identical == 9 * len(datasets)):
        raise ValueError("Validation failed: checked['fortran_raw_identical_files'] == raw_identical == 9 * len(datasets)")
    if not (checked['fortran_raw_files_checked'] == raw_checked == 10 * len(datasets)):
        raise ValueError("Validation failed: checked['fortran_raw_files_checked'] == raw_checked == 10 * len(datasets)")
    return len(cpp_expected)


def main():
    if not (sys.version_info >= (3, 11)):
        raise ValueError('The release checker requires Python 3.11 or newer')
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', required=True, type=Path, help='Explicit staged public package; never inferred from a development checkout')
    ap.add_argument('--base', required=True, help='Selected aggregate evidence basename, turbohap_benchmarks_DDMMYY')
    ap.add_argument('--write-manifest', action='store_true', help='Write SHA256SUMS from the Git-tracked package only')
    args = ap.parse_args()
    global ROOT, BASE, SUFFIX, GENERATED
    ROOT = args.root.resolve(strict=True)
    BASE = args.base
    SUFFIX, GENERATED = select_evidence(BASE)
    if not (not args.write_manifest or (ROOT / '.git').exists()):
        raise ValueError('Write the manifest from an explicitly staged Git tree')
    data = json.loads((ROOT / 'docs' / f'{BASE}.json').read_text())
    if not (release_suffix(data) == SUFFIX):
        raise ValueError('Validation failed: release_suffix(data) == SUFFIX')
    version = data['release_tag'].removeprefix('v')
    check_public_boundary(ROOT, package_files(), version)
    if not (re.fullmatch('\\d+\\.\\d+\\.\\d+', version)):
        raise ValueError("Validation failed: re.fullmatch('\\\\d+\\\\.\\\\d+\\\\.\\\\d+', version)")
    citation = (ROOT / 'CITATION.cff').read_text()
    if not (re.search('^version: ' + re.escape(version) + '$', citation, re.M)):
        raise ValueError("Validation failed: re.search('^version: ' + re.escape(version) + '$', citation, re.M)")
    if not (re.search('^date-released: [\\\'"]?' + re.escape(data['date']) + '[\\\'"]?$', citation, re.M)):
        raise ValueError('Validation failed: re.search(\'^date-released: [\\\\\\\'"]?\' + re.escape(data[\'date\']) + \'[\\\\\\\'"]?$\', citation, re.M)')
    if not (f'project(TurboHap VERSION {version} ' in (ROOT / 'CMakeLists.txt').read_text()):
        raise ValueError("Validation failed: f'project(TurboHap VERSION {version} ' in (ROOT / 'CMakeLists.txt').read_text()")
    if not (f'**Version {version}.**' in (ROOT / 'README.md').read_text()):
        raise ValueError("Validation failed: f'**Version {version}.**' in (ROOT / 'README.md').read_text()")
    if not (f'## {version} ' in (ROOT / 'CHANGELOG.md').read_text()):
        raise ValueError("Validation failed: f'## {version} ' in (ROOT / 'CHANGELOG.md').read_text()")
    expected_sources = {'CMakeLists.txt'} | {
        str(p.relative_to(ROOT)) for folder in ('src', 'cmake')
        for p in (ROOT / folder).rglob('*') if p.is_file()}
    if not (set(data['release_source_sha256']) == expected_sources):
        raise ValueError('Source manifest must cover every src/ and cmake/ file')
    for name, expected in data['release_source_sha256'].items():
        path = ROOT / name
        if not (path.stat().st_size == expected['bytes'] and digest(path) == expected['sha256']):
            raise ValueError(name)
    checked = json.loads((ROOT / f'docs/turbohap_validation_{SUFFIX}.json').read_text())
    cpp_runs = check_validation(data, checked)
    build = data['release_build_validation']
    if not (build['cmake_project_version'] == version):
        raise ValueError("Validation failed: build['cmake_project_version'] == version")
    if not (build['archive_build_completed']):
        raise ValueError('Packaged release build qualification is incomplete')
    if not (type(build['ctest_total']) is int and build['ctest_total'] > 0):
        raise ValueError("Validation failed: type(build['ctest_total']) is int and build['ctest_total'] > 0")
    if not (build['ctest_passed'] == build['ctest_total']):
        raise ValueError('Packaged regression tests have not all passed')
    check_build_identity(data)
    if not (build['strict_raw_output_checks'] == checked['strict_raw_output_checks']):
        raise ValueError("Validation failed: build['strict_raw_output_checks'] == checked['strict_raw_output_checks']")

    with tempfile.TemporaryDirectory(prefix='turbohap-release-check-') as tmp:
        temp = Path(tmp)
        (temp / 'docs/assets').mkdir(parents=True)
        shutil.copy2(ROOT / 'README.md', temp / 'README.md')
        shutil.copy2(ROOT / 'docs' / f'{BASE}.json', temp / 'docs' / f'{BASE}.json')
        shutil.copy2(ROOT / f'docs/turbohap_validation_{SUFFIX}.json', temp / f'docs/turbohap_validation_{SUFFIX}.json')
        os.environ.setdefault('MPLCONFIGDIR', str(temp / 'mpl-cache'))
        spec = importlib.util.spec_from_file_location('release_renderer', ROOT / 'tools/render_benchmarks.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.ROOT = temp
        module.BASE = BASE
        module.main()
        stale = [name for name in GENERATED if not (ROOT / name).exists()
                 or (ROOT / name).read_bytes() != (temp / name).read_bytes()]
        if not (not stale):
            raise ValueError('Stale generated release files: ' + ', '.join(stale))

    names = package_files()
    # Check relative document/image targets without crawling external websites.
    for name in names:
        if not name.endswith('.md'):
            continue
        text = (ROOT / name).read_text()
        for match in re.finditer(r'!?\[[^\]]*\]\(([^\s)]+)(?:\s+"[^"]*")?\)', text):
            target = urlsplit(match.group(1).strip('<>'))
            if target.scheme or target.netloc or not target.path:
                continue
            path = (ROOT / name).parent / unquote(target.path)
            if not (path.exists()):
                raise ValueError(f'{name}: missing local link {match.group(1)}')
    manifest = ROOT / 'SHA256SUMS'
    if args.write_manifest:
        manifest.write_text(''.join(f'{digest(ROOT / name)}  {name}\n' for name in names))
    if not (manifest.exists()):
        raise ValueError('Missing SHA256SUMS; stage package files and run --write-manifest first')
    entries = [line.split('  ', 1) for line in manifest.read_text().splitlines()]
    if not ([name for _, name in entries] == names):
        raise ValueError('Manifest file selection differs from the package')
    for expected, name in entries:
        if not (digest(ROOT / name) == expected):
            raise ValueError(f'Checksum mismatch: {name}')
    print(f'PASS: v{version}; generated prose, tables and six figures; complete source identities; versions; validation; local links; {len(entries)} file checksums.')
    print(f'Retained evidence: {build["ctest_total"]} passing packaged regression tests and {cpp_runs} C++ benchmark observations. This package check does not rerun them.')


if __name__ == '__main__':
    main()
