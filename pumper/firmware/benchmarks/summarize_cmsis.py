#!/usr/bin/env python3
"""Recompute optimized-EQ results without relaxing the original timing gates."""
import csv
import json
from pathlib import Path

root = Path(__file__).resolve().parent / 'results' / 'cmsis'
rows = list(csv.DictReader((root / 'numeric.csv').open()))
safe = [r for r in rows if r['range_failure'] == '0']
preset = [r for r in safe if r['fixture'] == 'current-preset' and
          r['rate'] == '96000' and r['dbfs'] == '-6']
summary = {
    'numeric': {
        'cases': len(rows), 'range_safe': len(safe),
        'within_response_tolerance': sum(float(r['response_error_db']) <= .05 for r in safe),
        'current_preset_min_improvement_db': min(float(r['improvement_db']) for r in preset),
        'current_preset_max_response_error_db': max(float(r['response_error_db']) for r in preset),
        'note': 'Finite test vectors are not arbitrary-input overflow bounds.'},
    'hardware': []}
for name in ['hardware.json', 'assembly-hardware.json']:
    if not (root / name).exists():
        continue
    for r in json.loads((root / name).read_text()):
        e = {k: r[k] for k in ['mode', 'scenario', 'rate', 'bits', 'boost', 'seconds', 'error']}
        e['benchmark_sha256'] = r['benchmark_sha256']
        if r['samples']:
            s = r['samples'][-1]
            avg = s['total_us'] * r['rate'] / s['frames'] / 10000
            peak = s['peak_us'] * r['rate'] / s['peak_frames'] / 10000
            delta = s['underruns'] - r['underruns_before']
            e.update(clock_mhz=s['clock']/1000000,
                     avg_total_percent=round(avg, 3), peak_total_percent=round(peak, 3),
                     underruns=delta, low_water_frames=s['low_water'],
                     max_spectrum_slice_us=s['spectrum_slice_us'],
                     max_temperature_c=max(x['temperature_mc'] for x in r['samples'])/1000,
                     selftest=s.get('selftest'), arithmetic_checks=s['arithmetic_checks'],
                     timing_gate=avg <= 70 and peak <= 85 and delta == 0 and not r['error'])
        else:
            e['timing_gate'] = False
        summary['hardware'].append(e)
(root / 'summary.json').write_text(json.dumps(summary, indent=2, allow_nan=False) + '\n')
print(json.dumps(summary['numeric'], indent=2))
print('| Kernel | MHz | Scenario | Average | Peak | Underrun frames |')
print('|---|---:|---|---:|---:|---:|')
for e in summary['hardware']:
    if 'avg_total_percent' not in e:
        print(e)
        continue
    kernel = 'CMSIS' if e['mode'] == 4 else 'M33 assembly'
    scenario = ['Steady', 'Crossfeed + limiter', 'Repeated ramps'][e['scenario']]
    print(f"| {kernel} | {e['clock_mhz']:.0f} | {scenario} | "
          f"{e['avg_total_percent']:.1f}% | {e['peak_total_percent']:.1f}% | {e['underruns']:,} |")
