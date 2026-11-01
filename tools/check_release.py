#!/usr/bin/env python3
"""Check the complete public package, including generated claims and file hashes."""
import argparse
from collections import Counter
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

from release_summary import release_suffix

ROOT = Path(__file__).resolve().parents[1]
BASE = 'turbohap_benchmarks_011126'
SUFFIX = BASE.rsplit('_', 1)[1]
GENERATED = ['README.md', f'docs/{BASE}.csv', f'docs/{BASE}.md',
             f'docs/turbohap_release_claims_{SUFFIX}.json',
             f'docs/turbohap_release_notes_{SUFFIX}.md']
GENERATED += ['docs/assets/' + BASE + suffix + '.' + ext
              for suffix in ['', '_speedup', '_memory'] for ext in ['png', 'svg']]


def digest(path):
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def package_files():
    if (ROOT / '.git').exists():
        names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')
        return sorted(n for n in names if n and n != 'SHA256SUMS')
    return sorted(str(p.relative_to(ROOT)) for p in ROOT.rglob('*')
                  if p.is_file() and p != ROOT / 'SHA256SUMS' and '__pycache__' not in p.parts)


def observation_key(row):
    return row['dataset'], row['program'], row['repeat']


def check_validation(data, checked):
    """Join every public timed observation to its retained output evidence."""
    assert checked['version'] == data['release_tag'].removeprefix('v')
    observations = data['observations']
    datasets = {r['dataset'] for r in observations}
    assert len(datasets) == 7
    assert data['repeats'] == {'Fortran baseline': 1, 'TurboHap': 2, 'TurboHap LM': 2}
    expected = Counter((d, p, r) for d in datasets for p, n in data['repeats'].items()
                       for r in range(1, n + 1))
    assert Counter(map(observation_key, observations)) == expected, 'Missing or duplicate timed observations'
    assert all(r['wall_seconds'] > 0 and r['peak_rss_kib'] > 0 and r['swaps'] == 0
               and r['output_check'].startswith('pass') for r in observations)
    cpp_expected = Counter({key: count for key, count in expected.items() if key[1] != 'Fortran baseline'})
    assert Counter(map(observation_key, checked['checks'])) == cpp_expected
    assert all(r['raw_passed'] and len(r['outputs']) == 10 for r in checked['checks'])
    assert checked['strict_raw_output_checks'] == sum(len(r['outputs']) for r in checked['checks']) == 280
    reference_outputs = {}
    for row in checked['checks']:
        outputs = row['outputs']
        for result in outputs.values():
            assert type(result['bytes']) is int and result['bytes'] >= 0
            assert re.fullmatch(r'[0-9a-f]{64}', result['sha256'])
        previous = reference_outputs.setdefault(row['dataset'], outputs)
        assert outputs == previous, f"Output identities differ across repeats/modes: {row['dataset']}"
    fortran = checked['fortran_checks']
    assert Counter(map(observation_key, fortran)) == Counter(
        {key: count for key, count in expected.items() if key[1] == 'Fortran baseline'})
    assert checked['fortran_exact_records_passed']
    for row in fortran:
        assert row['passed'] and row['exact_record_equivalence_passed']
        assert not row['raw_passed'] and row['raw_mismatches'] == ['cross.overs']
        assert len(row['files']) == 10 and all(result['passed'] for result in row['files'].values())
        assert {name for name, result in row['files'].items() if not result['raw_match']} == {'cross.overs'}
        assert row['files']['cross.overs']['canonical_match']
        assert row['files']['cross.overs']['canonical']['final_newline']
        assert row['files']['cross.overs']['canonical']['header_hex']
        assert set(row['outputs']) == set(reference_outputs[row['dataset']])
        for name, identity in row['outputs'].items():
            if name != 'cross.overs':
                assert identity == reference_outputs[row['dataset']][name]
    raw_identical = sum(result['raw_match'] for row in fortran for result in row['files'].values())
    raw_checked = sum(len(row['files']) for row in fortran)
    assert checked['fortran_raw_identical_files'] == raw_identical == 63
    assert checked['fortran_raw_files_checked'] == raw_checked == 70
    return len(cpp_expected)


def main():
    assert sys.version_info >= (3, 11), 'The release checker requires Python 3.11 or newer'
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--write-manifest', action='store_true', help='Write SHA256SUMS from the Git-tracked package only')
    args = ap.parse_args()
    assert not args.write_manifest or (ROOT / '.git').exists(), 'Write the manifest from an explicitly staged Git tree'
    data = json.loads((ROOT / 'docs' / f'{BASE}.json').read_text())
    assert release_suffix(data) == SUFFIX
    version = data['release_tag'].removeprefix('v')
    assert re.fullmatch(r'\d+\.\d+\.\d+', version)
    citation = (ROOT / 'CITATION.cff').read_text()
    assert re.search(r'^version: ' + re.escape(version) + r'$', citation, re.M)
    assert re.search(r'^date-released: [\'"]?' + re.escape(data['date']) + r'[\'"]?$', citation, re.M)
    assert f'project(TurboHap VERSION {version} ' in (ROOT / 'CMakeLists.txt').read_text()
    assert f'**Version {version}.**' in (ROOT / 'README.md').read_text()
    assert f'## {version} ' in (ROOT / 'CHANGELOG.md').read_text()
    expected_sources = {'CMakeLists.txt'} | {
        str(p.relative_to(ROOT)) for folder in ('src', 'cmake')
        for p in (ROOT / folder).rglob('*') if p.is_file()}
    assert set(data['release_source_sha256']) == expected_sources, 'Source manifest must cover every src/ and cmake/ file'
    for name, expected in data['release_source_sha256'].items():
        path = ROOT / name
        assert path.stat().st_size == expected['bytes'] and digest(path) == expected['sha256'], name
    checked = json.loads((ROOT / f'docs/turbohap_validation_{SUFFIX}.json').read_text())
    cpp_runs = check_validation(data, checked)
    build = data['release_build_validation']
    assert build['cmake_project_version'] == version
    assert build['archive_build_completed'], 'Packaged release build qualification is incomplete'
    assert type(build['ctest_total']) is int and build['ctest_total'] > 0
    assert build['ctest_passed'] == build['ctest_total'], 'Packaged regression tests have not all passed'
    assert build['metadata_only_change_from_measured_source']
    assert build['strict_raw_output_checks'] == checked['strict_raw_output_checks']

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
        module.main()
        stale = [name for name in GENERATED if not (ROOT / name).exists()
                 or (ROOT / name).read_bytes() != (temp / name).read_bytes()]
        assert not stale, 'Stale generated release files: ' + ', '.join(stale)

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
            assert path.exists(), f'{name}: missing local link {match.group(1)}'
    manifest = ROOT / 'SHA256SUMS'
    if args.write_manifest:
        manifest.write_text(''.join(f'{digest(ROOT / name)}  {name}\n' for name in names))
    assert manifest.exists(), 'Missing SHA256SUMS; stage package files and run --write-manifest first'
    entries = [line.split('  ', 1) for line in manifest.read_text().splitlines()]
    assert [name for _, name in entries] == names, 'Manifest file selection differs from the package'
    for expected, name in entries:
        assert digest(ROOT / name) == expected, f'Checksum mismatch: {name}'
    print(f'PASS: v{version}; generated prose, tables and six figures; complete source identities; versions; validation; local links; {len(entries)} file checksums.')
    print(f'Retained evidence: {build["ctest_total"]} passing packaged regression tests and {cpp_runs} C++ benchmark observations. This package check does not rerun them.')


if __name__ == '__main__':
    main()
