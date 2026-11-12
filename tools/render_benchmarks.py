#!/usr/bin/env python3
"""Regenerate v1.5.0 release presentation from aggregate observations only."""
import csv
import json
from pathlib import Path
import re
from statistics import geometric_mean
import os
os.environ.setdefault('MPLCONFIGDIR', '/tmp/turbohap-release-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.ticker import FuncFormatter, NullLocator, PercentFormatter
import numpy as np
from release_summary import PROGRAMS, claims, duration, summaries, validate

ROOT = Path(__file__).resolve().parents[1]
BASE = 'turbohap_benchmarks_111126'
COMPARISON = 'turbohap_release_comparison_111126'
COLORS = {'GNU FindHap': '#17365d', 'TurboHap': '#007db5', 'TurboHap LM': '#008b78'}
MARKERS = {'GNU FindHap': 'o', 'TurboHap': 's', 'TurboHap LM': 'D'}
INK = '#172c42'
MUTED = '#40556a'
DATASET_HEADING = 'Genotyped animals × loci'
DATASET_DEPOSIT = (
    'The exact inputs for all nine benchmarks are available on Zenodo. '
    '[Dataset DOI 10.5281/zenodo.24144449](https://doi.org/10.5281/zenodo.24144449)'
)
GENERATED = ['README.md', f'docs/{BASE}.md', f'docs/{BASE}.csv',
             'docs/turbohap_release_claims_111126.json', 'docs/turbohap_release_notes_111126.md',
             f'docs/{COMPARISON}.md'] + [
             f'docs/assets/{stem}.{ext}' for stem in (BASE, f'{BASE}_speedup', f'{BASE}_memory', COMPARISON)
             for ext in ('png', 'svg')]


def table(headers, rows):
    return '\n'.join(['| ' + ' | '.join(headers) + ' |', '| ' + ' | '.join('---' for _ in headers) + ' |'] +
                     ['| ' + ' | '.join(row) + ' |' for row in rows])


def update_block(path, name, text, *, inline_end=False):
    value = path.read_text()
    pattern = rf'<!-- {name}_START -->.*?<!-- {name}_END -->'
    if len(re.findall(pattern, value, re.S)) != 1:
        raise ValueError(f'Missing or duplicate generated block {name}: {path}')
    separator = ' ' if inline_end else '\n'
    path.write_text(re.sub(pattern, lambda _: f'<!-- {name}_START -->\n{text}{separator}<!-- {name}_END -->', value, flags=re.S))


def opening_example(data, values):
    dataset = next(d for d in data['datasets'] if d['id'] == 'n250000_m80000')
    rows = []
    baseline = values.get((dataset['id'], PROGRAMS[0]))
    for program in PROGRAMS:
        measured = values.get((dataset['id'], program))
        if measured is None:
            rows.append((program, 'Pending', '—'))
            continue
        seconds = measured['seconds']
        if seconds >= 3600:
            hours, minutes = divmod(round(seconds / 60), 60)
            runtime = f'{hours} h {minutes} min'
        else:
            minutes, seconds = divmod(round(seconds), 60)
            runtime = f'{minutes} min {seconds:02d} s' if minutes else f'{seconds} s'
        memory = f"{measured['gib']:.1f} GiB"
        if baseline is not None and program != PROGRAMS[0]:
            runtime += f" ({baseline['seconds'] / measured['seconds']:.1f}× faster)"
            memory += f" ({100 * (1 - measured['gib'] / baseline['gib']):.1f}% reduction)"
        rows.append((program, runtime, memory))
    return (
        f"Here’s what that choice looked like on a simulated dairy cattle dataset with "
        f"**{dataset['animals']:,} genotyped animals and {dataset['loci']:,} loci**. "
        f"We ran the same analysis using {data['threads']} CPU threads on an NVIDIA DGX Spark.\n\n"
        + table(['Program', 'Run time', 'Peak memory'], rows)
    )


def dataset_label(dataset):
    return f"{dataset['animals']:,} × {dataset['loci']:,}"


def figure_rows(ax, datasets, *, bands=False):
    ax.set_yticks(range(len(datasets)), [dataset_label(d) for d in datasets])
    ax.set_ylim(len(datasets) - 0.5, -0.5)
    ax.tick_params(axis='y', length=0, pad=10, labelsize=12, labelcolor=INK)
    if bands:
        for i, dataset in enumerate(datasets):
            if i % 2 == 0:
                ax.axhspan(i - 0.5, i + 0.5, facecolor='#f1f5f8', zorder=0)
            if i and dataset['animals'] != datasets[i - 1]['animals']:
                ax.axhline(i - 0.5, color='#c8d5df', linewidth=0.9, zorder=2)


def save_figure(fig, suffix, *, base=BASE):
    fig.savefig(ROOT / f'docs/assets/{base}{suffix}.png', dpi=180, facecolor='white')
    fig.savefig(ROOT / f'docs/assets/{base}{suffix}.svg', metadata={'Date': None}, facecolor='white')
    plt.close(fig)


def absolute_figure(data, values):
    datasets = data['datasets']
    fig, axes = plt.subplots(1, 2, figsize=(10.8, 6.8), sharey=True)
    panels = (
        ('Elapsed time', 'seconds', 'wall_seconds', 1, (0.8, 44000),
         [1, 10, 60, 600, 3600, 36000], ['1 s', '10 s', '1 min', '10 min', '1 h', '10 h'], 'Log scale'),
        ('Peak memory', 'gib', 'peak_rss_kib', 2**20, (0.22, 130),
         [0.25, 1, 4, 16, 64], ['0.25', '1', '4', '16', '64'], 'GiB · log scale'),
    )
    for ax, (title, metric, field, divisor, limits, ticks, labels, axis_label) in zip(axes, panels):
        figure_rows(ax, datasets, bands=True)
        for i, dataset in enumerate(datasets):
            for program, offset in zip(PROGRAMS, (-0.24, 0, 0.24)):
                measured = values.get((dataset['id'], program))
                if measured is None:
                    continue
                mean = measured[metric]
                observations = [row[field] / divisor for row in measured['rows']]
                whiskers = [[mean - min(observations)], [max(observations) - mean]] if len(observations) > 1 else None
                ax.errorbar(mean, i + offset, xerr=whiskers, fmt=MARKERS[program], color=COLORS[program],
                            markersize=6.5, markeredgecolor='white', markeredgewidth=0.65,
                            linewidth=1.25, capsize=2, zorder=3)
        ax.set_xscale('log')
        ax.set_xlim(*limits)
        ax.set_xticks(ticks, labels)
        ax.xaxis.set_minor_locator(NullLocator())
        ax.set_xlabel(axis_label, color=MUTED, labelpad=8)
        ax.set_title(title, loc='left', color=INK, fontsize=14, weight='bold', pad=9)
        ax.grid(axis='x', color='#a8b9c7', alpha=0.45, linewidth=0.75)
        ax.spines['left'].set_visible(False)
        ax.spines['bottom'].set_color('#c8d5df')
        ax.tick_params(axis='x', length=0, pad=7)
    handles = [
        Line2D([], [], linestyle='', marker=MARKERS[p], color=COLORS[p], label=p, markersize=7.5)
        for p in PROGRAMS
    ]
    fig.suptitle('Run time and peak memory by dataset',
                 x=0.05, y=0.958, ha='left', color=INK, fontsize=16, weight='bold')
    fig.legend(handles=handles, loc='upper left', bbox_to_anchor=(0.043, 0.91),
               ncol=3, frameon=False, columnspacing=1.5, labelcolor=INK, fontsize=11)
    fig.text(0.222, 0.833, DATASET_HEADING, ha='right', color=INK, weight='bold', fontsize=11)
    fig.text(0.05, 0.048, f"NVIDIA DGX Spark · {data['threads']} CPU threads · Points are means; whiskers show the observed range across repeats.",
             color=MUTED, fontsize=9.5)
    if any(d['gnu_status'] != 'complete' for d in datasets):
        missing = [dataset_label(d) for d in datasets if d['gnu_status'] != 'complete']
        fig.text(0.05, 0.023, f"GNU FindHap results unavailable for {' and '.join(missing)}.", color=MUTED, fontsize=9.5)
    fig.subplots_adjust(left=0.235, right=0.967, bottom=0.155, top=0.81, wspace=0.17)
    save_figure(fig, '')


def relative_figure(data, values, *, memory):
    datasets = data['datasets']
    ratios = np.full((len(datasets), 2), np.nan)
    for i, dataset in enumerate(datasets):
        ds = dataset['id']
        baseline = values.get((ds, PROGRAMS[0]))
        if baseline is None:
            continue
        for j, program in enumerate(PROGRAMS[1:]):
            measured = values[ds, program]
            ratios[i, j] = (100 * (1 - measured['gib'] / baseline['gib']) if memory
                            else baseline['seconds'] / measured['seconds'])
    fig, ax = plt.subplots(figsize=(8.4, 6.1))
    cmap = plt.get_cmap('Blues').with_extremes(bad='#e8edf1')
    maximum = 100 if memory else max(20, np.ceil(np.nanmax(ratios) / 20) * 20)
    minimum = min(0, np.floor(np.nanmin(ratios) / 20) * 20)
    image = ax.imshow(np.ma.masked_invalid(ratios), cmap=cmap, aspect='auto',
                      vmin=minimum, vmax=maximum, interpolation='nearest')
    figure_rows(ax, datasets)
    ax.xaxis.tick_top()
    ax.set_xticks([0, 1], PROGRAMS[1:])
    ax.tick_params(axis='x', length=0, pad=10, labelsize=12)
    for label, program in zip(ax.get_xticklabels(), PROGRAMS[1:]):
        label.set_color(COLORS[program])
        label.set_weight('bold')
    for spine in ax.spines.values():
        spine.set_visible(False)
    group_boundaries = [
        i - 0.5 for i in range(1, len(datasets))
        if datasets[i]['animals'] != datasets[i - 1]['animals']
    ]
    # The missing comparison spans both columns, with no centre divider.
    compared_rows = np.flatnonzero(np.isfinite(ratios).all(axis=1))
    ax.vlines(np.full(compared_rows.shape, 0.5), compared_rows - 0.5, compared_rows + 0.5,
              color='white', linewidth=1.25, zorder=2)
    ax.hlines(group_boundaries, -0.5, 1.5, color='white', linewidth=1.5, zorder=2)
    unit = '%' if memory else '×'
    for i, dataset in enumerate(datasets):
        if dataset['gnu_status'] != 'complete':
            label = 'GNU FindHap pending' if dataset['gnu_status'] == 'pending' else 'No GNU FindHap run'
            ax.text(0.5, i, label, ha='center', va='center', fontsize=11, color=MUTED)
            continue
        for j, value in enumerate(ratios[i]):
            rgb = cmap(image.norm(value))[:3]
            linear = [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055)**2.4 for x in rgb]
            luminance = sum(x * w for x, w in zip(linear, (0.2126, 0.7152, 0.0722)))
            ax.text(j, i, f'{value:.1f}{unit}', ha='center', va='center',
                    fontsize=13, weight='bold', color='white' if luminance < 0.20 else '#102337')
    title = 'Peak memory saved compared with GNU FindHap' if memory else 'Runtime speedup over GNU FindHap'
    fig.suptitle(title, x=0.055, y=0.955, ha='left', color=INK, fontsize=17, weight='bold')
    fig.text(0.055, 0.835, DATASET_HEADING, color=INK, weight='bold', fontsize=11)
    cax = fig.add_axes((0.886, 0.19, 0.024, 0.58))
    colorbar = fig.colorbar(image, cax=cax, format=FuncFormatter(lambda value, _: f'{value:g}{unit}'))
    colorbar.outline.set_visible(False)
    colorbar.ax.tick_params(length=0, pad=6, labelsize=10)
    calculation = ('Reduction in mean peak memory relative to GNU FindHap.' if memory
                   else 'Mean GNU FindHap runtime ÷ mean runtime for each mode.')
    fig.text(0.055, 0.075, f"NVIDIA DGX Spark · {data['threads']} CPU threads · {calculation}",
             color=MUTED, fontsize=10)
    fig.subplots_adjust(left=0.29, right=0.86, bottom=0.135, top=0.805)
    save_figure(fig, '_memory' if memory else '_speedup')


def figures(data, values):
    with plt.rc_context({'font.family': 'DejaVu Sans', 'font.size': 11,
                         'axes.spines.top': False, 'axes.spines.right': False,
                         'svg.hashsalt': 'turbohap-150-release'}):
        absolute_figure(data, values)
        relative_figure(data, values, memory=False)
        relative_figure(data, values, memory=True)


def matched_ratios(rows, version, baseline):
    return tuple(
        geometric_mean(row['measurements'][version][metric] / row['measurements'][baseline][metric]
                       for row in rows)
        for metric in ('seconds', 'peak_rss_kib')
    )


def version_comparison_lead(comparison, *, compact=False):
    baseline, candidate = comparison['baseline'], comparison['candidate']
    pairs = comparison['pairs']
    standard = [p for p in pairs if p['mode'] == 'standard']
    low_memory = [p for p in pairs if p['mode'] == 'LM']
    count = len(standard)
    standard_datasets = {p['dataset'] for p in standard}
    lm_datasets = {p['dataset'] for p in low_memory}
    if not count or count != len(low_memory) or standard_datasets != lm_datasets or len(standard_datasets) != count:
        raise ValueError('Version comparison needs one matched pair per dataset and mode')

    runtime_standard, memory_ratio_standard = matched_ratios(standard, candidate, baseline)
    runtime_lm, memory_ratio_lm = matched_ratios(low_memory, candidate, baseline)
    speed_standard, speed_lm = 1 / runtime_standard, 1 / runtime_lm
    memory_standard = 100 * (1 - memory_ratio_standard)
    memory_lm = 100 * (1 - memory_ratio_lm)
    interim = ' checked so far' if count < comparison['planned_datasets'] else ''
    if compact:
        return (
            f"- Across [{count} matched datasets](docs/{COMPARISON}.md){interim}, "
            f"{candidate} delivers a **{speed_standard:.2f}× speedup with {memory_standard:.0f}% lower peak memory** "
            f"in standard mode and a **{speed_lm:.2f}× speedup with {memory_lm:.0f}% lower peak memory** "
            f"in LM compared with {baseline}."
        )
    return (
        f"- **Faster, with lower peak memory than {baseline}.** Across {count} matched datasets{interim}, "
        f"standard TurboHap delivers a **{speed_standard:.2f}× speedup** with a "
        f"**{memory_standard:.0f}% reduction in peak memory**. "
        f"TurboHap LM delivers a **{speed_lm:.2f}× speedup** with a "
        f"**{memory_lm:.0f}% reduction in peak memory**. "
        f"Tests used {comparison['threads']} CPU threads on an NVIDIA DGX Spark, one run per version and mode "
        "on each dataset. Summaries use geometric means of the paired ratios. "
        f"[Compare all six releases]({COMPARISON}.md)."
    )


def render_release_comparison(comparison, datasets):
    versions = comparison['versions']
    first, latest = versions[0], comparison['candidate']
    modes = {'TurboHap': 'standard', 'TurboHap LM': 'LM'}
    by_program = {program: [r for r in comparison['pairs'] if r['mode'] == mode]
                  for program, mode in modes.items()}
    ratios = {program: [matched_ratios(rows, version, first) for version in versions]
              for program, rows in by_program.items()}
    dataset_info = {dataset['id']: dataset for dataset in datasets}
    label_color = '#516675'
    title_color = '#354f60'
    # Shared limits preserve the same meaning of colour in both modes. Slight
    # negative savings remain visible numerically rather than being clipped.
    limits = ((1, 6.5), (-1, 90))
    ticks = ((1, 2, 3, 4, 5, 6), (0, 20, 40, 60, 80))
    metrics = ('Relative speed', 'Peak memory reduction (%)')

    with plt.rc_context({'font.family': 'DejaVu Sans', 'font.size': 11,
                         'svg.hashsalt': 'turbohap-release-comparison'}):
        fig, axes = plt.subplots(2, 2, sharey='row', figsize=(13.8, 11.2))
        fig.suptitle('TurboHap performance across six releases', fontsize=19,
                     fontweight='bold', color=title_color, y=0.975)
        fig.text(0.5, 0.937, f'{comparison["hardware"]} · {comparison["threads"]} CPU threads · '
                 f'Each dataset and mode compared with its own {first} baseline',
                 ha='center', color=label_color, fontsize=10.5)
        images = [None, None]
        for column, (program, rows) in enumerate(by_program.items()):
            measured = np.array([
                [[row['measurements'][first]['seconds'] / row['measurements'][version]['seconds'],
                  100 * (1 - row['measurements'][version]['peak_rss_kib'] /
                         row['measurements'][first]['peak_rss_kib'])]
                 for version in versions]
                for row in rows
            ])
            averages = np.array([[1 / time, 100 * (1 - memory)]
                                 for time, memory in ratios[program]])
            values = np.concatenate((measured, averages[np.newaxis, :, :]), axis=0)
            labels = [dataset_label(dataset_info[row['dataset']]) for row in rows] + ['Average']
            for metric, ax in enumerate(axes[:, column]):
                cells = values[:, :, metric]
                im = ax.imshow(cells, cmap='YlGnBu', vmin=limits[metric][0], vmax=limits[metric][1],
                               interpolation='nearest', aspect='auto')
                images[metric] = im
                ax.set_title(f'{program} · {metrics[metric]}', loc='left',
                             fontweight='bold', color=title_color, fontsize=13, pad=13)
                ax.set_yticks(range(len(labels)), labels)
                ax.set_xticks(range(len(versions)), versions)
                ax.tick_params(axis='both', length=0, pad=8, labelcolor=label_color, labelsize=10.5)
                ax.spines[:].set_visible(False)
                # One consistent divider distinguishes the summary from observed datasets.
                ax.axhline(len(rows) - 0.5, color='#8b9ca5', linewidth=1.2)
                for label in ax.get_yticklabels():
                    if label.get_text() == 'Average':
                        label.set_fontweight('bold')
                for r in range(len(labels)):
                    for c in range(len(versions)):
                        value = cells[r, c]
                        decimals = 2 if metric == 0 else 1
                        display = round(value, decimals)
                        if display == 0:
                            display = 0.0  # Avoid negative zero in rounded annotations only.
                        red, green, blue, _ = im.cmap(im.norm(value))
                        linear = [v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
                                  for v in (red, green, blue)]
                        luminance = sum(v * w for v, w in zip(linear, (0.2126, 0.7152, 0.0722)))
                        color = 'white' if luminance < 0.25 else INK
                        ax.text(c, r, f'{display:.{decimals}f}', ha='center', va='center',
                                color=color, fontsize=10.5,
                                fontweight='bold' if r == len(rows) else 'normal')
        for metric in range(2):
            bar = fig.colorbar(images[metric], ax=axes[metric, :], cax=fig.add_axes(
                [0.94, 0.565 if metric == 0 else 0.155, 0.014, 0.30]))
            bar.set_ticks(ticks[metric])
            bar.ax.tick_params(length=0, pad=6, labelsize=10, labelcolor=label_color)
            bar.outline.set_visible(False)
        axes[0, 0].set_ylabel('Genotyped animals × loci', color=label_color, labelpad=12)
        axes[1, 0].set_ylabel('Genotyped animals × loci', color=label_color, labelpad=12)
        fig.text(0.53, 0.055, 'Higher values mean faster execution or less peak memory.',
                 ha='center', fontsize=10.5, color=label_color)
        fig.text(0.53, 0.030, 'Average: geometric mean of dataset ratios, with equal weight for each dataset.',
                 ha='center', fontsize=10, color=label_color)
        fig.subplots_adjust(left=0.20, right=0.915, top=0.87, bottom=0.135,
                            hspace=0.35, wspace=0.09)
        save_figure(fig, '', base=COMPARISON)

    improvements = []
    for baseline in (comparison['baseline'], first):
        for program, rows in by_program.items():
            runtime_ratio, memory_ratio = matched_ratios(rows, latest, baseline)
            speed = f'{1 / runtime_ratio:.2f}×'
            memory = f'{100 * (1 - memory_ratio):.0f}%'
            improvements.append((baseline, program, speed, memory))
    report = [
        '# TurboHap performance across releases',
        '',
        f'Across eight matched datasets, {latest} ranked first for runtime and peak memory in both modes.',
        '',
        f'![Four heatmaps showing relative speed and peak memory reduction for TurboHap and LM, '
        f'with eight dataset rows and an average row in each panel]'
        f'(assets/{COMPARISON}.png)',
        '',
        f'Each row shows one dataset. **Relative speed is {first} runtime divided by the newer runtime** '
        f'(2 means twice as fast). **Peak memory reduction** is the percentage reduction from {first}. '
        'Higher values are better in all four panels; the colour scale is shared between modes for each metric. '
        'The average row gives each dataset equal weight: it uses the geometric mean of the speed ratios, '
        'and converts the geometric mean of the memory-use ratios into a percentage reduction.',
        '',
        f'**Improvements in {latest}**',
        '',
        table(['Compared with', 'Mode', 'Fold speedup', 'Less peak memory'], improvements),
        '',
        '**How we compared the releases.** We ran both modes on the same eight simulated dairy cattle datasets '
        '(10,000 to 250,000 genotyped animals, each at 20,000 and 80,000 loci). We used one measurement '
        f'per version, dataset and mode, with matching analysis settings and {comparison["threads"]} CPU threads '
        f'on an {comparison["hardware"]}. We built each release with {comparison["compiler"]} using its release '
        f'defaults. Comparisons always use the same dataset and mode in the reference release.',
        '',
        DATASET_DEPOSIT,
        '',
        f'**Output checks.** {comparison["output_check_note"]}',
        '',
        f'[Underlying measurements (JSON, `matched_release_comparison`)]({BASE}.json) · '
        f'[{latest} versus GNU FindHap]({BASE}.md)',
        '',
    ]
    (ROOT / f'docs/{COMPARISON}.md').write_text('\n'.join(report))


def main():
    data=json.loads((ROOT/f'docs/{BASE}.json').read_text());validate(data)
    s=summaries(data); c=claims(data)
    (ROOT/'docs/turbohap_release_claims_111126.json').write_text(json.dumps(c,indent=2)+'\n')
    rows=[];mem=[];caps=[]
    for d in data['datasets']:
        ds=d['id'];label=dataset_label(d);g=s.get((ds,PROGRAMS[0]))
        a,b=(s[ds,p] for p in PROGRAMS[1:]);missing='Pending' if d['gnu_status']=='pending' else 'Not run'
        rows.append([label,duration(g['seconds']) if g else missing,duration(a['seconds']),
                     f"{g['seconds']/a['seconds']:.1f}×" if g else '—',duration(b['seconds']),f"{g['seconds']/b['seconds']:.1f}×" if g else '—'])
        mem.append([label,f"{g['gib']:.3f}" if g else missing,f"{a['gib']:.3f}",f"{100*(1-a['gib']/g['gib']):.1f}%" if g else '—',
                    f"{b['gib']:.3f}",f"{100*(1-b['gib']/g['gib']):.1f}%" if g else '—'])
        peak=max(r['peak_live_haplotypes'] for r in data['observations'] if r['dataset']==ds and r['program']!='GNU FindHap')
        caps.append([label,f"{d['maxhap']:,}",f'{peak:,}','2 + 2',str(g['n']) if g else missing])
    tables='### Runtime\n\n'+table([DATASET_HEADING,'GNU FindHap','TurboHap','Speedup','TurboHap LM','Speedup'],rows)+'\n\n### Peak memory\n\nMemory in GiB; reductions are relative to GNU FindHap.\n\n'+table([DATASET_HEADING,'GNU FindHap','TurboHap','Reduction','TurboHap LM','Reduction'],mem)
    fastest = next(d for d in data['datasets'] if d['id'] == c['max_speedup_dataset'])
    headline = (
        f"On the {fastest['animals']:,}-animal, {fastest['loci']:,}-locus dataset, "
        f"{c['max_speedup_program']} ran **{c['max_speedup']:.1f}× faster** than GNU FindHap."
    )
    update_block(ROOT / 'README.md', 'OPENING_EXAMPLE', opening_example(data, s))
    update_block(ROOT / 'README.md', 'HEADLINE', headline)
    update_block(ROOT/'README.md','BENCHMARK_TABLES',tables)
    pending = any(d['gnu_status']=='pending' for d in data['datasets'])
    control_note = 'The 250,000 × 80,000 FindHap control is pending. ' if pending else 'All eight planned FindHap controls are complete. '
    summary=f"This comparison includes **36 TurboHap measurements**, **{c['GNU_observations']} GNU FindHap measurements** and **360 passing raw TurboHap output-file checks**. The largest completed matched-control speedup is **{c['max_speedup']:.1f}×**; the largest measured memory reduction is **{c['max_memory_reduction_percent']:.1f}%**. {control_note}No FindHap control is planned for 1,000,000 × 80,000 because of time and resource limits."
    update_block(ROOT/'docs/turbohap_release_notes_111126.md','BENCHMARK_SUMMARY',summary)
    if 'matched_release_comparison' in data:
        comparison = data['matched_release_comparison']
        update_block(ROOT / 'README.md', 'VERSION_COMPARISON',
                     version_comparison_lead(comparison, compact=True), inline_end=True)
        update_block(ROOT / 'docs/turbohap_release_notes_111126.md', 'VERSION_COMPARISON',
                     version_comparison_lead(comparison), inline_end=True)
        render_release_comparison(comparison, data['datasets'])
    fields=['id','dataset','animals','loci','program','repeat','threads','maxhap','wall_seconds','user_seconds','system_seconds','peak_rss_kib','exit_status','swaps','date','peak_live_haplotypes']
    with (ROOT/f'docs/{BASE}.csv').open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=fields,lineterminator='\n');writer.writeheader();writer.writerows(data['observations'])
    lines=['# TurboHap v1.5.0 benchmark results' + (' — release draft' if data['status']=='draft' else ''),'',summary,'',
       f'![Elapsed time and peak memory by dataset and program](assets/{BASE}.png)','',tables,'',
       f'![Runtime speedup over GNU FindHap by dataset and mode](assets/{BASE}_speedup.png)','',f'![Peak memory saved compared with GNU FindHap by dataset and mode](assets/{BASE}_memory.png)','',
       '## Datasets, settings and repetition','',
       'These simulated dairy cattle cohorts use real cattle chromosome lengths and public SNP-chip maps, structured pedigrees, linkage and mixed-density genotyping. They are more realistic and more challenging than the older release datasets. They are different workloads: these times should not be read as a direct speed comparison with v1.4.0. See the [dataset recipe](SYNTHETIC_DATASETS.md). Neither the datasets nor their generation code is included in this release.','',
       DATASET_DEPOSIT,'',
       table([DATASET_HEADING,'maxhap (all programs)','Observed peak live haplotypes','TurboHap + LM repeats','GNU repeats completed'],caps),'',
       'All runs use 20 threads and the same options for each dataset: four iterations, XChrom=0, maxlen=100, minlen=50, three segment-length steps, errate=0.004 and all ten outputs enabled. The error-tolerance option is not the simulated genotype error rate. maxlen and minlen count loci, not physical bases. The capacity values above are shared between programs; they are capacity limits, not numbers of retained haplotypes.','',
       'Two technical repetitions per TurboHap mode are included throughout. GNU FindHap has two repetitions for 10k, 25k and 100k cohorts, and ' + ('one planned for each 250k cohort. ' if pending else 'one for each 250k cohort. ') + 'Tables use arithmetic means calculated separately for runtime and memory; whiskers show observed minima and maxima, not confidence intervals. Two repeats do not establish universal speedups or statistical equivalence.','',
       '## Measurement method','',
       f'- Hardware: {data["hardware"]}. The GPU is not used.',
       f'- Compiler/runtime: {data["compiler"]}. C++ flags: `{data["cpp_flags"]}`. GNU Fortran flags: `{data["fortran_flags"]}`. No fast-math or profile-guided optimisation.',
       '- Measurements use complete executable elapsed time and process peak RSS from GNU time. RSS is reported in KiB in the CSV and divided by 1,048,576 for GiB. Reading, computation, buffered writing and LM scratch activity are inside the timer. Container startup, compilation, input warming, external fsync, output verification and archival are outside it.',
       '- Input and working-output files are on the internal NVMe SSD. Passing retained copies are archived to external storage after measurement. Runs are sequential; inputs are verified and warmed; output directories are fresh. All selected runs exited successfully without swaps.',
       '- Both programs request 20 workers with OMP_NUM_THREADS=20, OMP_DYNAMIC=FALSE and LC_ALL=C. No external taskset or added binding is used. TurboHap uses its automatic heterogeneous-core row scheduler on the Spark; GNU uses its own normal scheduling. At 20 workers both performance and efficiency cores are available.',
       '- TurboHap runs standard then LM in the first repetition and LM then standard in the second. GNU observations were collected in separate sessions. Technical repeats do not represent independent simulated populations.',
       '- Timed runs used modern STDOUT. Release builds default to legacy STDOUT. Use `--stdout=modern` to reproduce the timed console mode. Both modes passed the exact-output checks in [release qualification](RELEASE_CHECKS.md).','',
       '## Output verification','',
       f'All 360 TurboHap checks compare raw file sizes and SHA-256 hashes against retained qualified references, without normalization. The {c["GNU_observations"]} completed GNU runs passed all {10*c["GNU_observations"]} checks: {c["GNU_raw_identical_files"]} raw file matches and {c["GNU_record_order_exceptions"]} established crossover record-order exceptions. For cross.overs only, the header and complete records are compared with duplicates retained; the unsorted raw mismatch is not relabelled as byte identity. No other difference is accepted.',
       '',('The missing 250k × 80k control prevents a new direct GNU comparison for that dataset. ' if pending else '') + 'The million-animal observation is a TurboHap capacity/performance demonstration, with no claimed FindHap speedup. See [compatibility](COMPATIBILITY.md).','',
       f'[Individual observations (CSV)]({BASE}.csv) · [Aggregate evidence and verification statuses (JSON)]({BASE}.json)','',
       '## Regenerate the presentation','',
       'Install the pinned dependencies in `tools/requirements-reporting.txt`, then run `python3 tools/render_benchmarks.py`. The renderer uses the included aggregate evidence. Run `python3 tools/check_release.py --root . --release` to verify the figures, tables and package.','']
    (ROOT/f'docs/{BASE}.md').write_text('\n'.join(lines))
    figures(data,s)


if __name__=='__main__': main()
