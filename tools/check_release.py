#!/usr/bin/env python3
"""Check the complete public package, including generated claims and file hashes."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
BASE = 'turbohap_benchmarks_301026'
GENERATED = ['README.md', 'docs/'+BASE+'.csv', 'docs/'+BASE+'.md',
             'docs/turbohap_release_claims_301026.json', 'docs/turbohap_release_notes_301026.md']
GENERATED += ['docs/assets/'+BASE+suffix+'.'+ext
              for suffix in ['', '_speedup', '_memory'] for ext in ['png', 'svg']]


def digest(path):
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def package_files():
    if (ROOT/'.git').exists():
        names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')
        return sorted(n for n in names if n and n != 'SHA256SUMS')
    return sorted(str(p.relative_to(ROOT)) for p in ROOT.rglob('*')
                  if p.is_file() and p != ROOT/'SHA256SUMS' and '__pycache__' not in p.parts)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--write-manifest', action='store_true', help='Write SHA256SUMS from the Git-tracked package only')
    args = ap.parse_args()
    assert not args.write_manifest or (ROOT/'.git').exists(), 'Write the manifest from an explicitly staged Git tree'
    data = json.loads((ROOT/'docs'/f'{BASE}.json').read_text())
    version = data['release_tag'].removeprefix('v')
    assert re.search(r'^version: '+re.escape(version)+r'$', (ROOT/'CITATION.cff').read_text(), re.M)
    assert f'project(TurboHap VERSION {version} ' in (ROOT/'CMakeLists.txt').read_text()
    assert f'**Version {version}.**' in (ROOT/'README.md').read_text()
    assert f'## {version} ' in (ROOT/'CHANGELOG.md').read_text()
    for name, expected in data['release_source_sha256'].items():
        path = ROOT/name
        assert path.stat().st_size == expected['bytes'] and digest(path) == expected['sha256'], name
    checked = json.loads((ROOT/'docs/turbohap_validation_301026.json').read_text())
    assert checked['strict_raw_output_checks'] == 280 and len(checked['checks']) == 28
    assert checked['fortran_raw_identical_files'] == 63 and checked['fortran_raw_files_checked'] == 70
    assert checked['fortran_exact_records_passed']

    with tempfile.TemporaryDirectory(prefix='turbohap-release-check-') as tmp:
        temp = Path(tmp)
        (temp/'docs/assets').mkdir(parents=True)
        shutil.copy2(ROOT/'README.md', temp/'README.md')
        shutil.copy2(ROOT/'docs'/f'{BASE}.json', temp/'docs'/f'{BASE}.json')
        os.environ.setdefault('MPLCONFIGDIR', str(temp/'mpl-cache'))
        spec = importlib.util.spec_from_file_location('release_renderer', ROOT/'tools/render_benchmarks.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.ROOT = temp
        module.main()
        stale = [name for name in GENERATED if not (ROOT/name).exists() or (ROOT/name).read_bytes() != (temp/name).read_bytes()]
        assert not stale, 'Stale generated release files: '+', '.join(stale)

    names = package_files()
    # Check relative document/image targets without crawling external websites.
    for name in names:
        if not name.endswith('.md'):
            continue
        text = (ROOT/name).read_text()
        for match in re.finditer(r'!?\[[^\]]*\]\(([^\s)]+)(?:\s+"[^"]*")?\)', text):
            target = urlsplit(match.group(1).strip('<>'))
            if target.scheme or target.netloc or not target.path:
                continue
            path = (ROOT/name).parent/unquote(target.path)
            assert path.exists(), f'{name}: missing local link {match.group(1)}'
    manifest = ROOT/'SHA256SUMS'
    if args.write_manifest:
        manifest.write_text(''.join(f'{digest(ROOT/name)}  {name}\n' for name in names))
    assert manifest.exists(), 'Missing SHA256SUMS; stage package files and run --write-manifest first'
    entries = [line.split('  ', 1) for line in manifest.read_text().splitlines()]
    assert [name for _, name in entries] == names, 'Manifest file selection differs from the package'
    for expected, name in entries:
        assert digest(ROOT/name) == expected, f'Checksum mismatch: {name}'
    print(f'PASS: v{version}; generated prose, tables and six figures; source identities; versions; validation; local links; {len(entries)} file checksums.')
    print('The 19 packaged C++ tests and 28 benchmark runs are retained qualification evidence for unchanged scientific source; this check does not rerun them.')


if __name__ == '__main__':
    main()
