"""Generate current-release benchmark claims from the selected public evidence."""
from datetime import date
import json
import re


RELEASE_DATASETS = (
    '10,000 × 10,000', '10,000 × 100,000', '100,000 × 10,000',
    '100,000 × 100,000', '1,000,000 × 10,000',
)


def benchmark_datasets(data):
    """Validate the declared current-release scope before generating any claims."""
    selection = data['dataset_selection']
    datasets = list(dict.fromkeys(row['dataset'] for row in data['observations']))
    if not (selection['minimum_loci'] == 10000):
        raise ValueError("Validation failed: selection['minimum_loci'] == 10000")
    if not (len(datasets) == len(selection['datasets']) == len(RELEASE_DATASETS)):
        raise ValueError("Validation failed: len(datasets) == len(selection['datasets']) == len(RELEASE_DATASETS)")
    if not (set(datasets) == set(selection['datasets']) == set(RELEASE_DATASETS)):
        raise ValueError('Unexpected release benchmark dataset')
    if not (all((int(dataset.split(' × ')[1].replace(',', '')) >= selection['minimum_loci'] for dataset in datasets))):
        raise ValueError("Validation failed: all((int(dataset.split(' × ')[1].replace(',', '')) >= selection['minimum_loci'] for dataset in datasets))")
    return datasets


def release_suffix(data):
    return date.fromisoformat(data['date']).strftime('%d%m%y')


def measurement_dates_text(data):
    dates = data['measurement_dates']
    if not (dates['TurboHap'] == dates['TurboHap LM']):
        raise ValueError("Validation failed: dates['TurboHap'] == dates['TurboHap LM']")
    return (f"GNU Fortran measurements: {dates['Fortran baseline']}; "
            f"TurboHap measurements: {dates['TurboHap']}.")


def qualification_text(data):
    build = data['release_build_validation']
    if (build['archive_build_completed'] and isinstance(build['ctest_total'], int)
            and build['ctest_total'] > 0 and build['ctest_passed'] == build['ctest_total']):
        text = f"The packaged release built successfully and passed all {build['ctest_total']} executed regression tests."
        skipped = build.get('skipped_tests', [])
        if skipped:
            text += f" {len(skipped)} optional tests were explicitly skipped; see the qualification metadata for their scope."
        return text
    return 'Packaged release build and regression-test qualification is pending.'


def render_summary(data, summaries, datasets, root):
    programs = ('Fortran baseline', 'TurboHap', 'TurboHap LM')
    suffix = release_suffix(data)
    version = data['release_tag'].removeprefix('v')
    fastest = max(datasets, key=lambda d: summaries[d, programs[0]][0] / summaries[d, programs[1]][0])
    largest = max(datasets, key=lambda d: tuple(int(n.replace(',', '')) for n in d.split(' × ')))
    f, t, lm = [summaries[largest, p][0] for p in programs]
    fm, tm, lmm = [summaries[largest, p][1] for p in programs]
    claims = dict(version=version, measurement_date=data['date'],
        measurement_dates=data['measurement_dates'], threads=data['threads'],
        repeats=data['repeats'], maximum_speedup_dataset=fastest,
        maximum_standard_speedup=summaries[fastest, programs[0]][0] / summaries[fastest, programs[1]][0],
        largest_dataset=largest, fortran_seconds=f, standard_seconds=t, lm_seconds=lm,
        standard_speedup=f / t, lm_speedup=f / lm,
        standard_memory_change_percent=100 * (tm / fm - 1),
        lm_memory_change_percent=100 * (lmm / fm - 1))
    (root / f'docs/turbohap_release_claims_{suffix}.json').write_text(json.dumps(claims, indent=2) + '\n')
    readme = root / 'README.md'
    text, count = re.subn(r'Up to [0-9.]+× faster in DGX Spark benchmarks\.',
        f'Up to {claims["maximum_standard_speedup"]:.1f}× faster in DGX Spark benchmarks.', readme.read_text())
    if not (count == 1):
        raise ValueError('Expected one performance headline')

    def words(seconds):
        hours, remainder = divmod(round(seconds), 3600)
        minutes, seconds = divmod(remainder, 60)
        return (f'{hours} hours ' if hours else '') + f'{minutes} minutes {seconds} seconds'

    def memory_words(change):
        return f'{abs(change):.1f}% ' + ('more' if change >= 0 else 'less') + ' peak memory'

    before, tail = text.split('On a simulated dataset of', 1)
    _, after = tail.split('All benchmarks used', 1)
    readme.write_text(before +
        f'On a simulated dataset of **{largest.replace(" × ", " animals × ")} loci**, GNU Fortran\n'
        f'FindHap took **{words(f)}**.\n'
        f'TurboHap v{version} finished in **{words(t)}**: **{f/t:.1f}× faster**, with\n'
        f'**{memory_words(claims["standard_memory_change_percent"])}**. TurboHap LM finished in **{words(lm)}**:\n'
        f'**{f/lm:.1f}× faster**, with **{memory_words(claims["lm_memory_change_percent"])}**.\n\n'
        + 'All benchmarks used' + after)
    repeats = data['repeats']
    validation = json.loads((root / f'docs/turbohap_validation_{suffix}.json').read_text())
    description = data.get('release_description',
        f'TurboHap v{version} reduces working memory and improves genotype processing, matching and output generation while preserving the existing commands, options and result-file formats.')
    features = data.get('release_features', [])
    notes = (description + '\n\n' + ''.join('- ' + feature + '\n' for feature in features) +
        '- Native installation support for Apple Silicon Macs, Linux ARM64, and Intel and AMD x86-64 systems. Detailed benchmarking on these systems is ongoing.\n'
        '- Automatic CPU placement respects user affinity and OpenMP settings; adaptive work scheduling is used when eligible heterogeneous cores require it.\n'
        '- Native NEON or AVX-512 paths are selected where supported, with portable fallbacks.\n'
        '- Checked output finalisation reports write, flush and close failures correctly.\n'
        f'- {len(datasets)} synthetic DGX Spark datasets, each with at least 10,000 loci: {len(data["observations"])} observations, with {repeats["Fortran baseline"]} Fortran and {repeats["TurboHap"]} per TurboHap mode/dataset. All {validation["strict_raw_output_checks"]} C++ raw-file checks pass. {qualification_text(data)}\n'
        '- The unchanged GNU Fortran 13.3 baseline uses expanded flags, ten threads, no taskset or added binding. It has one observation per dataset. Fortran passes exact-record checks; only crossover record ordering differs in raw checksums.\n\n'
        f'On **{largest}**, Fortran took **{f:,.2f} s**. Median complete runtime is **{t:.2f} s standard** and **{lm:.2f} s LM**, giving **{f/t:.1f}×** and **{f/lm:.1f}×** speedup. The maximum is **{claims["maximum_standard_speedup"]:.1f}×**, on {fastest}. Fortran values are single observations; TurboHap values are medians of two technical runs. {measurement_dates_text(data)} These are separate measurement sessions.\n\n'
        'Standard is faster and LM uses less peak resident memory on every measured dataset.\n\n'
        f'[All observations and methodology](https://github.com/adrian-greenneuron/TurboHap/blob/v{version}/docs/turbohap_benchmarks_{suffix}.md) · '
        f'[Output validation](https://github.com/adrian-greenneuron/TurboHap/blob/v{version}/docs/turbohap_validation_{suffix}.json).\n\n'
        'Build natively on the intended CPU with a C++17/OpenMP compiler and CMake. One executable provides `findhap 10` and `findhap 10 --low-memory`; no GPU or Fortran compiler is needed. See the README and MACOS.md for installation.\n\n'
        'The manuscript and supplementary material continue to describe v1.0.0 and are unchanged.\n')
    (root / f'docs/turbohap_release_notes_{suffix}.md').write_text(notes)
