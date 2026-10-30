#!/usr/bin/env python3
"""Render release tables and figures from the included aggregate measurements."""
from release_summary import render_summary

import csv
import json
from pathlib import Path
from statistics import median

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import TwoSlopeNorm
from matplotlib.lines import Line2D
from matplotlib.ticker import FixedLocator, FuncFormatter, LogLocator, NullLocator
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
BASE = 'turbohap_benchmarks_301026'
PROGRAMS = ('Fortran baseline', 'TurboHap', 'TurboHap LM')


def duration(seconds):
    if seconds < 60:
        return f'{seconds:.2f}s'
    total = round(seconds)
    hours, remainder = divmod(total, 3600)
    minutes, seconds = divmod(remainder, 60)
    return (f'{hours}h ' if hours else '') + f'{minutes}m {seconds:02d}s'


def main():
    data = json.loads((ROOT / 'docs' / f'{BASE}.json').read_text())
    observations = data['observations']
    datasets = list(dict.fromkeys(r['dataset'] for r in observations))
    assert len(datasets) == 7 and len(observations) == 35
    repeats = data['repeats']
    assert repeats == {'Fortran baseline': 1, 'TurboHap': 2, 'TurboHap LM': 2}
    assert {r['program'] for r in observations} == set(PROGRAMS)
    summaries = {}
    for dataset in datasets:
        for program in PROGRAMS:
            runs = [r for r in observations if r['dataset'] == dataset and r['program'] == program]
            assert len(runs) == repeats[program] and {r['repeat'] for r in runs} == set(range(1, repeats[program]+1))
            assert all(r['swaps'] == 0 and r['output_check'].startswith('pass') for r in runs)
            summaries[dataset, program] = (
                median(r['wall_seconds'] for r in runs),
                median(r['peak_rss_kib'] for r in runs) / 1048576)
    datasets.sort(key=lambda d: summaries[d, PROGRAMS[0]][0])
    render_summary(data, summaries, datasets, ROOT)

    with (ROOT / 'docs' / f'{BASE}.csv').open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=list(dict.fromkeys(k for row in observations for k in row)), lineterminator='\n')
        writer.writeheader()
        writer.writerows(observations)

    time_table = ['| Animals × loci | Fortran baseline | TurboHap | Speedup | TurboHap LM | Speedup |',
                  '| --- | ---: | ---: | ---: | ---: | ---: |']
    for d in datasets:
        f, t, lm = [summaries[d, p][0] for p in PROGRAMS]
        time_table.append(f'| {d} | {duration(f)} | **{duration(t)}** | **{f/t:.1f}×** | {duration(lm)} | {f/lm:.1f}× |')
    memory_table = ['| Animals × loci | Fortran baseline | TurboHap | Memory change | TurboHap LM | Memory change |',
                    '| --- | ---: | ---: | ---: | ---: | ---: |']
    for d in datasets:
        f, t, lm = [summaries[d, p][1] for p in PROGRAMS]
        memory_table.append(f'| {d} | {f:.3f} GiB | {t:.3f} GiB | {100*(t/f-1):+.1f}% | **{lm:.3f} GiB** | **{100*(lm/f-1):+.1f}%** |')
    tables = ['### Runtime', '', 'Speedup uses the single expanded-flag Fortran measurement and the median of two TurboHap measurements per mode, all collected on 30 October 2026.', '', *time_table, '',
              '### Peak memory', '', 'Negative percentages mean less memory than Fortran.', '', *memory_table]
    readme = ROOT / 'README.md'
    before, remainder = readme.read_text().split('<!-- BENCHMARK_TABLES_START -->', 1)
    _, after = remainder.split('<!-- BENCHMARK_TABLES_END -->', 1)
    readme.write_text(before + '<!-- BENCHMARK_TABLES_START -->\n\n' + '\n'.join(tables)
                      + '\n\n<!-- BENCHMARK_TABLES_END -->' + after)
    lines = ['# TurboHap v1.2.0 benchmarks', '',
             '30 October 2026. Seven synthetic datasets; one Fortran measurement and two measurements per TurboHap mode and dataset.', '',
             'All 35 observations were collected on 30 October in separate studies. The refreshed GNU Fortran baseline uses expanded compiler flags with no taskset or added binding. The second Fortran replicate is deferred; all 28 previously collected TurboHap observations are unchanged. Historical baselines remain available separately.', '',
             'Fortran values are single observations; TurboHap values are medians of two technical runs per mode. Runtime and RSS medians are calculated independently. Bold marks the lowest recorded runtime or RSS and its corresponding ratio, not statistical significance. No Fortran variation estimate is available yet.', '', *tables]
    lines += ['', f'![Runtime and peak memory](assets/{BASE}.png)', '',
              f'[Download runtime and memory figure (SVG)](assets/{BASE}.svg)', '',
              f'![Speedup over Fortran](assets/{BASE}_speedup.png)', '',
              f'[Download speedup figure (SVG)](assets/{BASE}_speedup.svg)', '',
              f'![Memory change relative to Fortran](assets/{BASE}_memory.png)', '',
              f'[Download memory change figure (SVG)](assets/{BASE}_memory.svg)', '',
              '## Methodology', '',
              '- Hardware: NVIDIA DGX Spark; 20 logical CPU cores; Ubuntu 24.04 ARM64.',
              '- Ten OpenMP threads for every measured program. GCC/gfortran 13.3.0.',
              '- Fortran flags: `'+data['fortran_flags']+'`. No fast-math or profile-guided optimisation.',
              '- C++ release flags: `-O3 -march=native -funroll-loops -flto -ffp-contract=off -std=gnu++17 -fopenmp`; GNU linking uses `-flto-partition=1to1`. No fast-math or profile-guided optimisation.',
              '- The seven datasets were generated with genosimV4; dimensions are genotyped animals × map loci. Each pedigree also contains four ungenotyped founders. Chip membership and pedigree structure affect work; see the [generation recipe and input checksums](turbohap_benchmark_datasets_221026.md).',
              '- Wall time covers the complete executable, including reading inputs, computation, writing results and temporary-file I/O. Output validation is outside the timer.',
              '- Peak memory is process peak resident set size from GNU `time`, converted from KiB to GiB. It is not total system memory, filesystem cache or disk usage.',
              '- Each dataset used standard-1, LM-1, LM-2, standard-2 order. All 28 C++ observations are included unchanged. Fortran used one run per dataset in ascending animal count, then locus count; all seven are included.',
              '- Both C++ modes use the default automatic CPU policy: the full 20-core container allocation is available, and the launcher selects the ten Cortex-X925 cores (logical IDs 5–9 and 15–19). No taskset or additional worker binding is used. The new Fortran runs had access to all 20 cores with ten threads, no taskset and no added binding. Both C++ modes use `LC_ALL=C`, `OMP_NUM_THREADS=10` and `OMP_DYNAMIC=FALSE`; explicit placement variables are unset.',
              '- All primary elapsed times use GNU time (%e), excluding measurement-wrapper overhead. Fortran wrapper wall time is retained separately in the raw observations. Studies used different sessions and the placement policies above.',
              '- A clean release layout, with CMake project version 1.2.0, was frozen and built before measurement. All 19 packaged tests passed. Source and measured executable hashes are included in the JSON metadata. Every benchmark input matches its published SHA-256 and byte count, including the million-animal benchmark options.',
              '- Inputs were explicitly warmed and residency checked before each timed C++ run. There were no inference warmup runs or forced cache eviction. Outputs were fresh; post-exit checked file/directory fsync and hashing occurred outside the timer. Fortran inputs were verified before its study, with no forced cache eviction or inference warmups. No competing benchmark was intentionally run; unrelated background activity was not comprehensively traced. One Fortran observation and two C++ technical repeats do not establish confidence intervals, statistical equivalence or universal speedup.',
              '- All 280 C++ raw-file checks pass. Fortran passes 63/70 raw-file checks: only cross.overs record order differs. Its header and every complete record match after C-locale sorting with duplicates retained. These seven raw mismatches remain explicit; no other difference is accepted.', '',
              f'[All observations (CSV)]({BASE}.csv) · [Metadata and observations (JSON)]({BASE}.json) · [Raw output checks](turbohap_validation_301026.json) · [Compatibility](COMPATIBILITY.md)', '',
              '[Historical v1.1.0 benchmarks](turbohap_benchmarks_281026.md) · [Historical v1.0.0 benchmarks](turbohap_benchmarks_221026.md). The manuscript and supplementary files continue to describe v1.0.0; they have not been revised for this software release.', '',
              '## Regenerate the tables and figures', '',
              'With Python 3 and the versions in `tools/requirements-reporting.txt` installed, run `python3 tools/render_benchmarks.py` from the source directory. This reads the included aggregate JSON and does not access input genotypes or run either program.', '',
              'The same renderer updates the README headline, performance summary and release notes. See [release preparation](RELEASING.md) for complete-package checks and checksums.', '']
    (ROOT / 'docs' / f'{BASE}.md').write_text('\n'.join(lines))

    plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 11,
                         'axes.spines.top': False, 'axes.spines.right': False,
                         'svg.hashsalt': 'turbohap-release'})
    short = [d.replace('1,000,000', '1M').replace('100,000', '100k').replace('10,000', '10k').replace('1,000', '1k') for d in datasets]
    plot_runtime_memory(datasets, short, summaries, observations)

    for memory in (False, True):
        values = np.array([[100*(summaries[d, p][1]/summaries[d, PROGRAMS[0]][1]-1) if memory
                            else summaries[d, PROGRAMS[0]][0]/summaries[d, p][0]
                            for p in PROGRAMS[1:]] for d in datasets])
        fig, ax = plt.subplots(figsize=(7.5, 6))
        kwargs = {'cmap': 'RdYlGn_r', 'norm': TwoSlopeNorm(vmin=-50, vcenter=0, vmax=5)} if memory else {'cmap':'YlOrRd', 'vmin':0, 'vmax':175}
        im = ax.imshow(values, aspect='auto', **kwargs)
        ax.set_xticks(range(2), PROGRAMS[1:]); ax.set_yticks(range(len(datasets)), short)
        ax.set_ylabel('Animals × loci')
        ax.set_title('Peak memory change relative to Fortran' if memory else 'Speedup relative to Fortran')
        for i in range(len(datasets)):
            for j in range(2):
                val=values[i,j]
                dark = (val < -35 or val > 3) if memory else val > 65
                ax.text(j, i, f'{val:+.1f}%' if memory else f'{val:.1f}×',
                        ha='center', va='center', color='white' if dark else '#172033', weight='bold', fontsize=13)
        bar = fig.colorbar(im, ax=ax, label='Change in peak RSS (%)' if memory else 'Fortran time / C++ time (×)')
        if memory:
            bar.set_ticks([-50, -40, -30, -20, -10, 0, 5])
        fig.tight_layout()
        save(fig, BASE + ('_memory' if memory else '_speedup'))


def plot_runtime_memory(datasets, labels, summaries, observations):
    colors = dict(zip(PROGRAMS, ['#17365d', '#2878c8', '#864ba2']))
    markers = dict(zip(PROGRAMS, ['o', 'P', 'D']))
    marker_sizes = dict(zip(PROGRAMS, [10, 12, 10]))
    with plt.rc_context({'font.size': 10}):
        fig, ax = plt.subplots(figsize=(12, 8), layout='constrained')
        ax.set_xscale('log')
        ax.set_yscale('log')
        all_times, all_memory = [], []
        for dataset, label in zip(datasets, labels):
            points = []
            for program in PROGRAMS:
                time, memory = summaries[dataset, program]
                runs = [r for r in observations if r['dataset'] == dataset and r['program'] == program]
                times = [r['wall_seconds'] for r in runs]
                memories = [r['peak_rss_kib'] / 1048576 for r in runs]
                if len(runs)==1:
                    ax.plot(time,memory,marker=markers[program],color=colors[program],linestyle='',
                        markersize=marker_sizes[program],markeredgecolor='white',markeredgewidth=.8,zorder=4)
                else:
                    ax.errorbar(time, memory,
                            xerr=[[time - min(times)], [max(times) - time]],
                            yerr=[[memory - min(memories)], [max(memories) - memory]],
                            fmt=markers[program], color=colors[program],
                            markersize=marker_sizes[program],
                            markeredgecolor='white', markeredgewidth=.8,
                            elinewidth=1.5, capsize=3, capthick=1.5, zorder=4)
                points.append((time, memory))
                all_times.extend(times)
                all_memory.extend(memories)
            ax.plot([p[0] for p in points], [p[1] for p in points],
                    color='#8b97a5', linewidth=1.8, zorder=1)
            offset = (8, -18) if label in ('10k × 1k', '10k × 100k', '100k × 100k') else (8, 10)
            ax.annotate(label, points[0], xytext=offset, textcoords='offset points',
                        fontsize=10, color='#303840')
        ax.set_xlim(min(all_times) / 1.55, max(all_times) * 2.5)
        ax.set_ylim(min(all_memory) / 1.4, max(all_memory) * 1.55)

        def time_tick(value, _):
            if value < 60:
                return f'{value:g} sec'
            if value < 3600:
                return f'{value / 60:g} min'
            return f'{value / 3600:g} hr'

        ax.xaxis.set_major_locator(FixedLocator([.5, 1, 2, 5, 10, 20, 60, 120, 300, 600, 1200, 3600, 7200, 18000, 36000]))
        ax.xaxis.set_major_formatter(FuncFormatter(time_tick))
        ax.xaxis.set_minor_locator(NullLocator())
        ax.yaxis.set_major_locator(LogLocator(base=10, subs=(1, 2, 5)))
        ax.yaxis.set_major_formatter(FuncFormatter(lambda value, _: f'{value:g}'))
        ax.tick_params(which='minor', labelleft=False)
        ax.grid(which='major', color='#e2e6ea', linewidth=.6)
        ax.set_xlabel('Time taken')
        ax.set_ylabel('Peak memory (GiB)')
        ax.legend(handles=[Line2D([], [], marker=markers[p], color=colors[p], linestyle='',
                                  markersize=marker_sizes[p], markeredgecolor='white',
                                  markeredgewidth=.8, label=p) for p in PROGRAMS],
                  loc='upper left', frameon=False, ncols=3)
        fig.suptitle('TurboHap v1.2.0: runtime and memory', fontsize=17, weight='bold')
        ax.set_title('Seven simulated datasets · 10 CPU threads · Fortran: 1 run; each TurboHap mode: 2 runs', fontsize=11)
        fig.supxlabel('Further left is faster. Lower means less memory. Labels: animals × loci.\n'
                      'Log axes · Fortran: single observations; C++: medians and observed ranges · 30 Oct 2026', fontsize=10)
        save(fig, BASE)


def save(fig, name):
    folder = ROOT / 'docs/assets'
    fig.savefig(folder / f'{name}.png', dpi=180, bbox_inches='tight')
    fig.savefig(folder / f'{name}.svg', bbox_inches='tight', metadata={'Date': None})
    svg = folder / f'{name}.svg'
    svg.write_text('\n'.join(line.rstrip() for line in svg.read_text().splitlines()) + '\n')
    plt.close(fig)


if __name__ == '__main__':
    main()
