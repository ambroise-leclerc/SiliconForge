#!/usr/bin/env python3
"""Linux dispatch benchmark: balanced randomized blocks, all samples retained, paired bootstrap intervals."""
import argparse
import datetime
import hashlib
import itertools
import json
import os
from pathlib import Path
import random
import re
import resource
import statistics
import subprocess
import sys
import time

EVENTS = 'cycles:u,instructions:u,branches:u,branch-misses:u,L1-dcache-loads:u,L1-dcache-load-misses:u'
NAMES = ('baseline', 'large', 'compact')


def percentile(values, fraction):
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lo = int(position)
    hi = min(lo + 1, len(ordered) - 1)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (position - lo)


def comparison(blocks, candidate, reference, seed, resamples):
    candidates = [b['runs'][candidate]['MHz'] for b in blocks]
    references = [b['runs'][reference]['MHz'] for b in blocks]
    deltas = [100 * (a / b - 1) for a, b in zip(candidates, references)]
    rng = random.Random(seed)
    paired_draws, ratio_draws = [], []
    for _ in range(resamples):
        indices = rng.choices(range(len(blocks)), k=len(blocks))
        paired_draws.append(statistics.median(deltas[i] for i in indices))
        ratio_draws.append(100 * (statistics.median(candidates[i] for i in indices) /
                                 statistics.median(references[i] for i in indices) - 1))
    return {'ratio_of_medians_delta_percent': 100 * (statistics.median(candidates) / statistics.median(references) - 1),
            'ratio_bootstrap_95_ci_percent': [percentile(ratio_draws, .025), percentile(ratio_draws, .975)],
            'paired_median_delta_percent': statistics.median(deltas),
            'paired_bootstrap_95_ci_percent': [percentile(paired_draws, .025), percentile(paired_draws, .975)],
            'paired_deltas_percent': deltas}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in NAMES:
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--firmware', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--cpu', type=int, default=0)
    parser.add_argument('--blocks', type=int, default=24, help='Samples per binary; multiple of six to balance all orders')
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--warmup-seconds', type=float, default=5)
    parser.add_argument('--seed', type=int, default=1729)
    parser.add_argument('--resamples', type=int, default=10000)
    parser.add_argument('--perf', action='store_true', help='Also attempt perf stat on all three binaries')
    parser.add_argument('--perf-repeats', type=int, default=5)
    args = parser.parse_args()
    if args.blocks < 6 or args.blocks % 6 or args.seconds <= 0 or args.warmup_seconds < 0 or args.resamples < 100:
        parser.error('Use a positive multiple of six blocks, seconds > 0, warmup >= 0 and resamples >= 100')
    if args.cpu not in os.sched_getaffinity(0):
        parser.error('Selected CPU is outside the allowed affinity mask')
    os.sched_setaffinity(0, {args.cpu})
    programs = {name: getattr(args, name).resolve() for name in NAMES}
    firmware = args.firmware.resolve()

    def command(name, seconds):
        return [str(programs[name]), '--headless', '--speed', 'max', '--duration', str(seconds), str(firmware)]

    result = {'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'configuration': {'cpu': args.cpu, 'blocks': args.blocks, 'simulated_seconds': args.seconds,
                                'warmup_seconds': args.warmup_seconds, 'seed': args.seed,
                                'bootstrap_resamples': args.resamples, 'firmware': str(args.firmware),
                                'perf_event_paranoid': Path('/proc/sys/kernel/perf_event_paranoid').read_text().strip()},
              'binaries': {name: {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                           for name, path in programs.items()}, 'perf_stat': {}, 'blocks': [],
              'interval_scope': 'Exploratory paired-block bootstrap on this session only; drift/autocorrelation may remain. No sample exclusion.'}
    if args.perf:
        for name in NAMES:
            cmd = ['perf', 'stat', '-x', ';', '-r', str(args.perf_repeats), '-e', EVENTS, '--', *command(name, args.seconds)]
            p = subprocess.run(cmd, capture_output=True, text=True)
            result['perf_stat'][name] = {'command': cmd, 'returncode': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr}
    if args.warmup_seconds:
        for name in NAMES:
            subprocess.run(command(name, args.warmup_seconds), capture_output=True, check=True)
    rng = random.Random(args.seed)
    schedule = []
    for _ in range(args.blocks // 6):
        orders = list(itertools.permutations(NAMES))
        rng.shuffle(orders)
        schedule.extend(orders)
    for index, order in enumerate(schedule):
        block = {'index': index, 'order': order, 'runs': {}}
        for name in order:
            usage_start = resource.getrusage(resource.RUSAGE_CHILDREN)
            wall_start = time.perf_counter()
            p = subprocess.run(command(name, args.seconds), capture_output=True, text=True, check=True)
            wall = time.perf_counter() - wall_start
            usage = resource.getrusage(resource.RUSAGE_CHILDREN)
            match = re.search(r'\(([\d.]+) MHz effective', p.stderr)
            if not match:
                raise ValueError('No emulation throughput in ' + p.stderr)
            block['runs'][name] = {'MHz': float(match[1]), 'process_wall_seconds': wall,
                                  'process_cpu_seconds': usage.ru_utime + usage.ru_stime - usage_start.ru_utime - usage_start.ru_stime,
                                  'voluntary_switches': usage.ru_nvcsw - usage_start.ru_nvcsw,
                                  'involuntary_switches': usage.ru_nivcsw - usage_start.ru_nivcsw, 'stderr': p.stderr}
        result['blocks'].append(block)
        args.output.write_text(json.dumps(result, indent=2) + '\n')
        print(f'block {index + 1}/{args.blocks}: ' + ', '.join(f'{name}={block["runs"][name]["MHz"]:.1f}' for name in NAMES), file=sys.stderr, flush=True)
    result['summary'] = {}
    for name in NAMES:
        values = [b['runs'][name]['MHz'] for b in result['blocks']]
        result['summary'][name] = {'n': len(values), 'median_MHz': statistics.median(values),
                                   'mean_MHz': statistics.mean(values), 'stdev_MHz': statistics.stdev(values),
                                   'min_MHz': min(values), 'max_MHz': max(values),
                                   'q1_MHz': percentile(values, .25), 'q3_MHz': percentile(values, .75)}
    result['comparisons'] = {f'{a}_vs_{b}': comparison(result['blocks'], a, b, args.seed + i, args.resamples)
                             for i, (a, b) in enumerate([('compact', 'baseline'), ('large', 'baseline'), ('compact', 'large')])}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'summary': result['summary'], 'comparisons': result['comparisons']}, indent=2))


if __name__ == '__main__':
    main()
