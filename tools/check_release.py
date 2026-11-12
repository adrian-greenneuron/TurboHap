#!/usr/bin/env python3
"""Check source-package boundaries, evidence, generated claims and checksums."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import tempfile
from urllib.parse import unquote, urlsplit
from release_summary import validate, require, claims
BASE='turbohap_benchmarks_111126'

ROLES={'production-source','build','regression-test','example','user-documentation',
       'benchmark-evidence','frozen-manuscript','release-tooling','licence-metadata'}
TOP={'src','cmake','tests','examples','docs','tools'}
ROOT_FILES={'CMakeLists.txt','README.md','MACOS.md','LICENSE','CHANGELOG.md','CITATION.cff',
            '.gitignore','.gitattributes','PUBLIC_PACKAGE_FILES.json','SHA256SUMS'}
FORBIDDEN={'experimental_input_validation.hpp','resource_safety.hpp','worker_errors.hpp',
           'test_sampled_validation.cpp','test_resource_safety.cpp','test_worker_failure.py',
           'allocation_fault.cpp','benchmark_conflict_scan.cpp'}


def digest(path):
    with path.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()


def files(root):
    answer=[]
    for path in root.rglob('*'):
        rel=path.relative_to(root)
        if '.git' in rel.parts or '__pycache__' in rel.parts:continue
        require(not path.is_symlink(),f'Symlink in package: {rel}')
        if path.is_file():answer.append(rel.as_posix())
    return sorted(answer)


def boundary(root):
    actual=files(root); policy=json.loads((root/'PUBLIC_PACKAGE_FILES.json').read_text())
    require(policy['schema_version']==1 and policy['release_version']=='1.5.0','Invalid package policy')
    require(set(actual)==set(policy['files']),'Unreviewed or missing package file')
    for name,role in policy['files'].items():
        p=Path(name)
        require(role in ROLES and not p.is_absolute() and '..' not in p.parts,'Invalid distribution role/path')
        require((len(p.parts)==1 and name in ROOT_FILES) or (len(p.parts)>1 and p.parts[0] in TOP),f'Unsupported public path: {name}')
        require(p.name not in FORBIDDEN and p.suffix!='.patch' and 'experimental' not in p.parts and 'variants' not in p.parts,f'Unpromoted experiment: {name}')
        if name=='CMakeLists.txt' or p.parts[0] in ('src','cmake'):
            text=(root/name).read_text()
            require(not any(v in text for v in ('FINDHAP_EXPERIMENTAL_','FINDHAP_FEATURE_DEFINITIONS','experimental/')),f'Unpromoted build hook: {name}')
        if p.suffix in ('.md','.json','.csv','.cff') and role!='frozen-manuscript':
            text=(root/name).read_text()
            require(not any(v in text for v in ('/home/adrian/','/mnt/ext_2/','/Users/adrian/','github.com/adrian-greenneuron/findhap/')),f'Private development detail: {name}')
    return actual


def check_generated(root):
    from render_benchmarks import GENERATED
    with tempfile.TemporaryDirectory(prefix='turbohap-release-check-') as tmp:
        target=Path(tmp);(target/'docs/assets').mkdir(parents=True)
        for name in ('README.md',f'docs/{BASE}.json','docs/turbohap_release_notes_111126.md'):
            shutil.copy2(root/name,target/name)
        spec=importlib.util.spec_from_file_location('release_render_checked',root/'tools/render_benchmarks.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);module.ROOT=target;module.main()
        stale=[name for name in GENERATED if not (root/name).exists() or (root/name).read_bytes()!=(target/name).read_bytes()]
        require(not stale,'Stale generated presentation: '+', '.join(stale))


def check(root, draft=True, write_manifest=False):
    actual=boundary(root);data=json.loads((root/f'docs/{BASE}.json').read_text());validate(data,final=not draft)
    require(json.loads((root/'docs/turbohap_release_claims_111126.json').read_text())==claims(data),'Stale summary claims')
    cmake=(root/'CMakeLists.txt').read_text()
    require('project(TurboHap VERSION 1.5.0 ' in cmake,'Wrong CMake version')
    require(re.search(r'option\(FINDHAP_MODERN_STDOUT_DEFAULT "[^"\n]*" OFF\)',cmake),'Legacy stdout must be the source-build default')
    require('#define FINDHAP_MODERN_STDOUT_DEFAULT 0' in (root/'src/stdout_policy.hpp').read_text(),'Direct engine build default must be legacy')
    require('version: 1.5.0\n' in (root/'CITATION.cff').read_text(),'Wrong citation version')
    if not draft:
        require(re.search(r'set\(TURBOHAP_VERSION_LABEL "\$\{PROJECT_VERSION\}" CACHE STRING ', cmake),
                'The default build label must match the project version')
        for name in actual:
            if Path(name).suffix in ('.md', '.cff'):
                require(not re.search(r'TurboHap_staging|\b(?:release draft|staging draft|draft release|draft checks)\b',
                                      (root/name).read_text(), flags=re.IGNORECASE),
                        f'Staging-only text in release document: {name}')
        require('date-released:' in (root/'CITATION.cff').read_text(), 'Final release date missing')
    require('## 1.5.0' in (root/'CHANGELOG.md').read_text(),'Missing changelog')
    build=json.loads((root/'docs/build_qualification_111126.json').read_text())
    require(build['source_build_completed'] is True and build['ctest_failed']==0,'Build/test qualification incomplete')
    require(build['legacy_default_verified'] is True and build['modern_option_verified'] is True,'Console mode qualification incomplete')
    require(build['production_modern_binary_matches_measured'] is True,'Benchmark/production engine identity not established')
    require(build['modern_binary_sha256']==data['measured_binary_sha256'],'Measured executable identity changed')
    require(build['ctest_executed']>0 and build['example_raw_output_checks']>=40,'Missing packaged inference checks')
    require(build.get('extracted_archive_build_completed') is True and build.get('extracted_archive_legacy_binary_matches') is True, 'Extracted archive build not qualified')
    require(build.get('installed_layout_verified') is True, 'Installed layout not qualified')
    source={n for n in actual if n=='CMakeLists.txt' or n.startswith(('src/','cmake/'))}
    require(source==set(build['source_sha256']),'Incomplete source identity')
    for name,value in build['source_sha256'].items():require(digest(root/name)==value,'Changed qualified source: '+name)
    for name in actual:
        if name.endswith('.md'):
            text=(root/name).read_text()
            for m in re.finditer(r'!?\[[^\]]*\]\(([^\s)]+)(?:\s+"[^\"]*")?\)',text):
                url=urlsplit(m[1].strip('<>'))
                if not url.scheme and not url.netloc and url.path:
                    require(((root/name).parent/unquote(url.path)).exists(),f'Broken local link: {name}: {m[1]}')
    check_generated(root)
    names=[n for n in actual if n!='SHA256SUMS']
    content=''.join(f'{digest(root/n)}  {n}\n' for n in names)
    if write_manifest:(root/'SHA256SUMS').write_text(content)
    require((root/'SHA256SUMS').read_text()==content,'Package checksums differ')
    print(f'PASS: {"draft" if draft else "final"} package; {len(names)} files; {len(data["observations"])} measurements; all evidence, source identities, figures, links and hashes checked.')
    if draft:print('Draft only; public publication not performed. Required pending controls: ' + ', '.join(d['id'] for d in data['datasets'] if d['gnu_status']=='pending'))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True)
    group=p.add_mutually_exclusive_group(required=True);group.add_argument('--draft',action='store_true');group.add_argument('--release',action='store_true')
    p.add_argument('--write-manifest',action='store_true');a=p.parse_args();check(a.root.resolve(),a.draft,a.write_manifest)


if __name__=='__main__':main()
