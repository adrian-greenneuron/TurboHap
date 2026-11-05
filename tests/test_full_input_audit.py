"""Synthetic multi-file audit cases; never use or publish private cohort records."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--engine', type=Path, required=True)
    args = parser.parse_args()
    binary, engine = args.binary.resolve(), args.engine.resolve()
    with tempfile.TemporaryDirectory(prefix='turbohap-full-audit-') as temporary:
        base = Path(temporary)
        cases = 0

        def fixture(name):
            root = base / name
            root.mkdir()
            (root / 'findhap.options').write_text('1 0 4 4 1 100 1 1 1 1\nlabels\n\n.004\n')
            (root / 'pedigree.file').write_text('M 101 -1 0 20000101\nF 102 0 0 20000102\nF 103 101 102 20050101\n')
            (root / 'chromosome.data').write_text('marker chr within overall pos chips chip1\n' +
                ''.join(f'm{i} 1 {i} {i} {i*10} 1 {i}\n' for i in range(1, 5)))
            (root / 'genotypes.txt').write_text(f'{103:10}{1:10}{4:10} 0125\n')
            (root / 'genotypes.filled').write_bytes(b'preserve result\n')
            return root

        def run(root, *flags, exit_code=None):
            nonlocal cases
            before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()}
            result = subprocess.run([str(binary), '--directory', str(root), '--json', *flags], capture_output=True, text=True)
            after = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()}
            if before != after:
                raise ValueError('Audit modified an input/result file')
            if exit_code is not None and result.returncode != exit_code:
                raise ValueError(f'{root.name}: exit {result.returncode}, expected {exit_code}: {result.stderr}\n{result.stdout[:2000]}')
            cases += 1
            return result, json.loads(result.stdout) if result.stdout else None

        def issue(data, code, count=None):
            found = [i for i in data['issues'] if i['code'] == code]
            if not found or (count is not None and sum(i['occurrences'] for i in found) != count):
                raise ValueError(f'Missing/wrong issue count: {code}, {found}')
            return found[0]

        root = fixture('clean')
        _, data = run(root, exit_code=0)
        if data['status'] != 'pass' or data['error_count']:
            raise ValueError('Clean fixture failed')

        root = fixture('multiple-files')
        (root / 'findhap.options').write_text('-1 0 4 4 1 100 7 1 1 1\nlabels\n\nnan\n')
        with (root / 'pedigree.file').open('a') as handle: handle.write('malformed\nM 103 0 0 20000101\n')
        (root / 'chromosome.data').write_text('header\nm1 1 1 2 1 1 1\n')
        (root / 'genotypes.txt').write_text(f'{103:10}{1:10}{4:10} 0 65\n')
        _, data = run(root, exit_code=2)
        for code in ('option_iters', 'option_hapout', 'invalid_error_rate', 'pedigree_missing_fields',
                     'duplicate_pedigree_id', 'map_overall_order', 'blank_genotype_fields', 'invalid_genotype_codes'):
            issue(data, code)
        if not data['blocked_checks']:
            raise ValueError('Invalid map dependency was not disclosed')

        root = fixture('counts')
        (root / 'genotypes.txt').write_text(''.join(f'{103:10}{1:10}{4:10} 0  5\n' for _ in range(50)))
        tsv = base / 'all_issues.tsv'
        prefix = base / 'input_audit_041126'
        result, data = run(root, '--max-examples', '2', '--issues-tsv', str(tsv), '--report-prefix', str(prefix), exit_code=2)
        blanks = issue(data, 'blank_genotype_fields', 50)
        if blanks['affected_values'] != 100 or len(blanks['examples']) != 2 or blanks['examples_omitted'] != 48:
            raise ValueError('Capped examples concealed/changed total counts')
        records = list(csv.DictReader(tsv.open(), delimiter='\t'))
        if len([r for r in records if r['code'] == 'blank_genotype_fields']) != 50:
            raise ValueError('Detailed TSV omitted issue events')
        saved = json.loads(prefix.with_suffix('.json').read_text())
        if saved != data or 'Suggested action:' not in prefix.with_suffix('.md').read_text():
            raise ValueError('Human/JSON reports disagree or lack repair guidance')
        rejection = subprocess.run([str(binary), '--directory', str(root), '--report-prefix', str(prefix)], capture_output=True)
        if rejection.returncode != 2:
            raise ValueError('Existing reports were overwritten')

        root = fixture('cycles')
        (root / 'pedigree.file').write_text('M 101 102 0 20000101\nF 102 101 0 20000102\nF 103 101 0 20050101\n')
        _, data = run(root, exit_code=2)
        issue(data, 'ancestry_cycle', 2)
        if data['metrics']['ancestry_cycle_components'] != 1:
            raise ValueError('Cycle membership confused with descendants')

        root = fixture('review-warnings')
        (root / 'pedigree.file').write_text('F 101 0 0 20100101\nF 103 101 999 20050101\n')
        _, data = run(root, exit_code=0)
        for code in ('parent_birth_order', 'parent_sex_conflict', 'unrepresented_parent'):
            issue(data, code)
        if data['status'] != 'warning':
            raise ValueError('Review issues silently passed or caused fatal rejection')

        root = fixture('overflow-schedule')
        (root / 'findhap.options').write_text('1 0 1 2147483647 2 100 1 1 1 1\nlabels\n\n.004\n')
        _, data = run(root, exit_code=2)
        issue(data, 'invalid_rounded_schedule')

        root = fixture('oversized-record')
        valid = (root / 'genotypes.txt').read_text()
        (root / 'genotypes.txt').write_text(valid.rstrip('\n') + '0' * 100000 + '\n' + valid)
        _, data = run(root, exit_code=2)
        issue(data, 'record_too_long', 1)
        if data['metrics']['genotype_records'] != 2:
            raise ValueError('Oversized-record recovery lost the next physical row')

        root = fixture('unusual-valid-options')
        (root / 'findhap.options').write_text('1,0,2*4,1,100,4*1\nlabels\n\n4D-3\n')
        _, data = run(root, exit_code=0)

        root = fixture('reserved-report-path')
        forbidden = root / 'fort.26'
        result = subprocess.run([str(binary), '--directory', str(root), '--issues-tsv', str(forbidden)], capture_output=True)
        if result.returncode != 2 or forbidden.exists():
            raise ValueError('A reserved result filename was opened for reporting')

        root = fixture('missing-required')
        for name in ('findhap.options', 'pedigree.file', 'chromosome.data', 'genotypes.txt'):
            (root / name).unlink()
        _, data = run(root, exit_code=2)
        issue(data, 'unreadable', 4)

        root = fixture('orphan-prior')
        (root / 'hap.foundpriors').write_text('unused')
        _, data = run(root, exit_code=0)
        issue(data, 'orphan_prior')

        root = fixture('prior-companions')
        (root / 'hap.listpriors').write_text('broken')
        _, data = run(root, exit_code=2)
        issue(data, 'missing_prior_companion', 2)

        # Use actual qualified engine exports, including multi-step sentinels,
        # to avoid testing an invented prior format against itself.
        root = base / 'exported-priors'
        shutil.copytree(Path(__file__).resolve().parents[1] / 'examples/small', root)
        result = subprocess.run([str(engine), '2'], cwd=root, capture_output=True)
        if result.returncode:
            raise ValueError('Could not generate reference priors')
        for source, destination in [('hap.list', 'hap.listpriors'), ('hap.found', 'hap.foundpriors')]:
            shutil.copy2(root / source, root / destination)
        _, data = run(root, exit_code=0)
        if data['error_count'] or not data['metrics']['prior_library_haplotypes']:
            raise ValueError('Original engine prior export failed the audit')

        priors = {name: (root / name).read_bytes() for name in ('hap.listpriors', 'hap.foundpriors', 'hap.Xseg')}
        def restore():
            for name, content in priors.items(): (root / name).write_bytes(content)
        rows = priors['hap.listpriors'].splitlines(keepends=True)
        edited = bytearray(rows[2]); edited[58] = ord('5'); rows[2] = bytes(edited)
        (root / 'hap.listpriors').write_bytes(b''.join(rows))
        _, data = run(root, exit_code=2); issue(data, 'invalid_prior_haplotype_codes'); restore()

        rows = priors['hap.foundpriors'].splitlines()
        fields = rows[2].split(); fields[4] = b'50000'; rows[2] = b' '.join(fields)
        (root / 'hap.foundpriors').write_bytes(b'\n'.join(rows) + b'\n')
        _, data = run(root, exit_code=2); issue(data, 'unknown_prior_haplotype'); restore()

        rows = priors['hap.listpriors'].splitlines(keepends=True)
        sentinel = next(i for i in range(2, len(rows)) if int(rows[i][20:30]) == 0)
        rows.pop(sentinel)
        (root / 'hap.listpriors').write_bytes(b''.join(rows))
        _, data = run(root, exit_code=2); issue(data, 'missing_prior_sentinel'); restore()

        rows = priors['hap.listpriors'].splitlines(keepends=True)
        first_segment = int(rows[2][36:43])
        for i in range(2, len(rows)):
            if int(rows[i][33:36]) == 1 and int(rows[i][36:43]) == first_segment:
                changed = bytearray(rows[i]); changed[43:51] = f'{2:8}'.encode(); rows[i] = bytes(changed)
        (root / 'hap.listpriors').write_bytes(b''.join(rows))
        _, data = run(root, exit_code=2); issue(data, 'uncovered_prior_loci'); restore()

        rows = priors['hap.foundpriors'].splitlines(keepends=True)
        rows.insert(3, rows[2])
        (root / 'hap.foundpriors').write_bytes(b''.join(rows))
        _, data = run(root, exit_code=2); issue(data, 'duplicate_prior_assignment'); restore()

        rows = priors['hap.listpriors'].splitlines(keepends=True)
        changed = bytearray(rows[0]); changed[31:34] = b'  2'; rows[0] = bytes(changed)
        (root / 'hap.listpriors').write_bytes(b''.join(rows))
        _, data = run(root, exit_code=2); issue(data, 'prior_header_steps_mismatch'); restore()

        (root / 'hap.Xseg').write_text('\n\n')
        _, data = run(root, exit_code=2); issue(data, 'blank_xseg_record', 2); restore()

        # A deep pedigree must use iterative traversal, not Python/C++ recursion.
        root = fixture('deep')
        count = 20000
        (root / 'pedigree.file').write_text(''.join(f'M {i} {i-1} 0 0\n' for i in range(1, count + 1)))
        (root / 'genotypes.txt').write_text(f'{count:10}{1:10}{4:10} 0125\n')
        _, data = run(root, exit_code=0)
        if data['metrics']['pedigree_unique_animals'] != count:
            raise ValueError('Deep pedigree was not completely traversed')

        print(f'PASS: {cases} complete input audits; multi-file errors, exact counts, report limits, '
              'private report safety, pedigree SCC/depth, real prior exports and cross-file references')


if __name__ == '__main__':
    main()
