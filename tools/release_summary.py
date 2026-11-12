"""Validate and summarize the selected v1.5.0 aggregate benchmark evidence."""
from collections import Counter
import math
import statistics

PROGRAMS = ('GNU FindHap', 'TurboHap', 'TurboHap LM')
DATASETS = [(n, m) for n in (10000, 25000, 100000, 250000) for m in (20000, 80000)] + [(1000000, 80000)]
CAPACITIES = [50000, 50000, 50000, 50000, 60000, 50000, 130000, 60000, 170000]
OUTPUTS = {'allele.frequency', 'cross.overs', 'do_not_distrib', 'genotypes.filled',
           'hap.filled', 'hap.found', 'hap.inherit', 'hap.list', 'hap.Xseg', 'haplotypes.txt'}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def validate(data, final=False):
    require(data['schema_version'] == 1 and data['release_version'] == '1.5.0', 'Wrong release schema/version')
    require(data['status'] in ('draft', 'release'), 'Unknown release status')
    if final:
        require(data['status'] == 'release', 'Draft cannot pass the final release gate')
    require(data['threads'] == 20 and data['summary'] == 'arithmetic mean', 'Benchmark method changed')
    ds = data['datasets']
    require([(d['animals'], d['loci']) for d in ds] == DATASETS, 'Nine selected datasets required')
    require([d['maxhap'] for d in ds] == CAPACITIES, 'Selected maxhap changed')
    expected = set()
    for d in ds:
        require(d['id'] == f"n{d['animals']}_m{d['loci']}", 'Dataset label mismatch')
        count = 2 if d['animals'] <= 100000 else 1 if d['animals'] == 250000 else 0
        require(d['gnu_expected_repeats'] == count, 'GNU replicate plan changed')
        status = d['gnu_status']
        require(status in ('pending', 'not-planned', 'complete'), 'Unknown control status')
        require((status == 'not-planned') == (count == 0), 'Invalid missing control')
        if status == 'pending':
            require(d['id'] == 'n250000_m80000' and not final, 'Unexpected pending control')
        for program in PROGRAMS:
            repeats = (count if status == 'complete' else 0) if program == PROGRAMS[0] else 2
            expected.update((d['id'], program, r) for r in range(1, repeats + 1))
    rows = data['observations']
    require(Counter((r['dataset'], r['program'], r['repeat']) for r in rows) == Counter(expected), 'Missing or duplicate observations')
    require(len({r['id'] for r in rows}) == len(rows), 'Duplicate observation ID')
    definitions = {d['id']: d for d in ds}
    for r in rows:
        d = definitions[r['dataset']]
        require(all(r[k] == d[k] for k in ('animals', 'loci', 'maxhap')) and r['threads'] == 20, 'Unmatched measurement settings')
        require(all(isinstance(r[k], (int, float)) and math.isfinite(r[k]) and r[k] > 0 for k in ('wall_seconds', 'peak_rss_kib')), 'Invalid runtime/RSS')
        require(r['exit_status'] == r['swaps'] == 0, 'Failed/swapping measurement')
    checks = data['validation']
    require(Counter(c['id'] for c in checks) == Counter(r['id'] for r in rows), 'Validation does not join observations')
    by_id = {r['id']: r for r in rows}
    for c in checks:
        files = c['files']
        require(len(files) == 10 and {f['file'] for f in files} == OUTPUTS, 'Incomplete output verification')
        for f in files:
            require(f['passed'] is True, 'Failed file check')
            require((f['raw_identical'] is True and f['record_order_exception'] is False) or
                    (by_id[c['id']]['program'] == 'GNU FindHap' and f['file'] == 'cross.overs' and
                     f['raw_identical'] is False and f['record_order_exception'] is True), 'New output compatibility exception')
    return rows


def summaries(data):
    validate(data)
    answer = {}
    for d in data['datasets']:
        for p in PROGRAMS:
            rows = [r for r in data['observations'] if r['dataset'] == d['id'] and r['program'] == p]
            if rows:
                answer[d['id'], p] = dict(n=len(rows), seconds=statistics.fmean(r['wall_seconds'] for r in rows),
                    gib=statistics.fmean(r['peak_rss_kib'] / 2**20 for r in rows), rows=rows)
    return answer


def claims(data):
    s = summaries(data)
    ratios = [(s[d['id'], PROGRAMS[0]]['seconds'] / s[d['id'], p]['seconds'], d['id'], p)
              for d in data['datasets'] if (d['id'], PROGRAMS[0]) in s for p in PROGRAMS[1:]]
    savings = [100 * (1 - s[d['id'], p]['gib'] / s[d['id'], PROGRAMS[0]]['gib'])
               for d in data['datasets'] if (d['id'], PROGRAMS[0]) in s for p in PROGRAMS[1:]]
    maximum = max(ratios)
    return dict(max_speedup=maximum[0], max_speedup_dataset=maximum[1], max_speedup_program=maximum[2],
                max_memory_reduction_percent=max(savings), TurboHap_observations=36,
                GNU_observations=sum(r['program'] == PROGRAMS[0] for r in data['observations']),
                TurboHap_raw_file_checks=360,
                GNU_raw_identical_files=sum(f['raw_identical'] for c in data['validation'] if '_GNU_' in c['id'] for f in c['files']),
                GNU_record_order_exceptions=sum(f['record_order_exception'] for c in data['validation'] for f in c['files']))


def duration(s):
    if s < 60:
        return f'{s:.2f} s'
    n = round(s)
    h, rem = divmod(n, 3600)
    m, sec = divmod(rem, 60)
    return (f'{h}h ' if h else '') + f'{m}m {sec:02}s'
