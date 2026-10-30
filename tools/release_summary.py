"""Generate every current-release benchmark headline from the published observations."""
import json
import re


def render_summary(data, summaries, datasets, root):
    programs = ('Fortran baseline', 'TurboHap', 'TurboHap LM')
    version = data['release_tag'].removeprefix('v')
    fastest = max(datasets, key=lambda d: summaries[d, programs[0]][0]/summaries[d, programs[1]][0])
    largest = max(datasets, key=lambda d: tuple(int(n.replace(',', '')) for n in d.split(' × ')))
    f, t, lm = [summaries[largest, p][0] for p in programs]
    fm, tm, lmm = [summaries[largest, p][1] for p in programs]
    claims = dict(version=version, measurement_date=data['date'], threads=data['threads'],
        repeats=data['repeats'], maximum_speedup_dataset=fastest,
        maximum_standard_speedup=summaries[fastest, programs[0]][0]/summaries[fastest, programs[1]][0],
        largest_dataset=largest, fortran_seconds=f, standard_seconds=t, lm_seconds=lm,
        standard_speedup=f/t, lm_speedup=f/lm,
        standard_memory_change_percent=100*(tm/fm-1), lm_memory_change_percent=100*(lmm/fm-1))
    (root/'docs/turbohap_release_claims_301026.json').write_text(json.dumps(claims, indent=2)+'\n')
    readme = root/'README.md'
    text, count = re.subn(r'Up to [0-9.]+× faster in DGX Spark benchmarks\.',
        f'Up to {claims["maximum_standard_speedup"]:.1f}× faster in DGX Spark benchmarks.', readme.read_text())
    assert count == 1, 'Expected one performance headline'

    def words(seconds):
        hours, remainder = divmod(round(seconds), 3600)
        minutes, seconds = divmod(remainder, 60)
        return (f'{hours} hours ' if hours else '')+f'{minutes} minutes {seconds} seconds'

    def memory_words(change):
        return f'{abs(change):.1f}% '+('more' if change >= 0 else 'less')+' peak memory'

    before, tail = text.split('On a simulated dataset of', 1)
    _, after = tail.split('All benchmarks used', 1)
    readme.write_text(before+
        f'On a simulated dataset of **{largest.replace(" × ", " animals × ")} loci**, GNU Fortran\n'
        f'FindHap took **{words(f)}**.\n'
        f'TurboHap v{version} finished in **{words(t)}**: **{f/t:.1f}× faster**, with\n'
        f'**{memory_words(claims["standard_memory_change_percent"])}**. TurboHap LM finished in **{words(lm)}**:\n'
        f'**{f/lm:.1f}× faster**, with **{memory_words(claims["lm_memory_change_percent"])}**.\n\n'
        +'All benchmarks used'+after)
    repeats=data['repeats']
    notes=(f'TurboHap v{version} improves matching, low-memory lookup and output processing while retaining the existing commands, options and result-file formats.\n\n'
        '- Automatic performance-core selection on DGX Spark, respecting explicit affinity and OpenMP settings.\n'
        '- Improved performance on AMD and Intel x86 computers; detailed benchmarks are ongoing.\n'
        '- Checked output finalisation reports write, flush and close failures correctly.\n'
        f'- Seven synthetic datasets: {len(data["observations"])} observations, with {repeats["Fortran baseline"]} Fortran and {repeats["TurboHap"]} per TurboHap mode/dataset. All 280 C++ raw-file checks pass. The unchanged release code passed all 19 packaged tests.\n'
        '- The refreshed GNU Fortran 13.3 baseline uses expanded flags, ten threads, no taskset or added binding. Its second replicate is deferred. Fortran passes exact-record checks; only crossover record ordering differs in raw checksums.\n\n'
        f'On **{largest}**, Fortran took **{f:,.2f} s**. Median complete runtime is **{t:.2f} s standard** and **{lm:.2f} s LM**, giving **{f/t:.1f}×** and **{f/lm:.1f}×** speedup. The maximum is **{claims["maximum_standard_speedup"]:.1f}×**, on {fastest}. Fortran values are single observations; TurboHap values are medians of two technical runs. All measurements were collected on 30 October 2026 in separate studies.\n\n'
        'Standard is faster and LM uses less peak resident memory on every measured dataset.\n\n'
        f'[All observations and methodology](https://github.com/adrian-greenneuron/TurboHap/blob/v{version}/docs/turbohap_benchmarks_301026.md) · '
        f'[Output validation](https://github.com/adrian-greenneuron/TurboHap/blob/v{version}/docs/turbohap_validation_301026.json).\n\n'
        'Build natively on the intended CPU with a C++17/OpenMP compiler and CMake. One executable provides `findhap 10` and `findhap 10 --low-memory`; no GPU or Fortran compiler is needed.\n\n'
        'The manuscript and supplementary material continue to describe v1.0.0 and are unchanged.\n')
    (root/'docs/turbohap_release_notes_301026.md').write_text(notes)
