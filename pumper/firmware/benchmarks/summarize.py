#!/usr/bin/env python3
"""Summarize the recorded numerical and hardware experiments without relaxing gates."""
import csv
import json
from pathlib import Path

root = Path(__file__).resolve().parent / 'results'
rows = list(csv.DictReader((root / 'numeric.csv').open()))
h3 = list(csv.DictReader((root / 'current-h3.csv').open()))
summary = {'numeric': {}, 'hardware': []}
for name in ['q31-state32', 'q31-state64']:
    subset = [r for r in rows if r['variant'] == name]
    safe = [r for r in subset if not any(int(r[k]) for k in ['overflow', 'clip', 'unstable'])]
    preset = [r for r in h3 if r['variant'] == name and r['fixture'] == 'current-preset' and r['rate'] == '96000' and r['dbfs'] == '-6']
    precision_pass = all(not any(int(r[k]) for k in ['overflow', 'clip', 'unstable']) and float(r['improvement_db']) >= 12 and float(r['response_error_db']) <= .05 for r in preset)
    summary['numeric'][name] = {
        'cases': len(subset), 'without_overflow_or_clipping': len(safe),
        'within_response_tolerance': sum(float(r['response_error_db']) <= .05 for r in safe),
        'current_preset_h3_min_improvement_db': min(float(r['improvement_db']) for r in preset),
        'current_preset_h3_max_response_error_db': max(float(r['response_error_db']) for r in preset),
        'current_preset_precision_gate': precision_pass,
        'headroom_bits_tested': [0, 30],
        'note': 'Passing a finite corpus is not an arbitrary-signal overflow bound.'}
if (root / 'hardware.json').exists():
    for r in json.loads((root / 'hardware.json').read_text()):
        if not r['samples']:
            summary['hardware'].append({k: r[k] for k in ['mode', 'scenario', 'rate', 'bits', 'boost', 'error']})
            continue
        s = r['samples'][-1]
        avg = s['total_us'] * r['rate'] / s['frames'] / 10000
        peak = s['peak_us'] * r['rate'] / s['peak_frames'] / 10000
        delta = s['underruns'] - r['underruns_before']
        entry = {k: r[k] for k in ['mode', 'scenario', 'rate', 'bits', 'boost', 'seconds']}
        entry.update(clock_mhz=s['clock']/1000000, avg_total_percent=round(avg, 3), peak_total_percent=round(peak, 3),
                     underruns=delta, overflows=s['overflows'], clips=s['clips'],
                     max_spectrum_slice_us=s['spectrum_slice_us'],
                     temperature_c=s['temperature_mc']/1000,
                     timing_gate=avg <= 70 and peak <= 85 and delta == 0 and not r.get('error'),
                     error=r.get('error'))
        summary['hardware'].append(entry)
(root / 'summary.json').write_text(json.dumps(summary, indent=2, allow_nan=False) + '\n')
print(json.dumps(summary['numeric'], indent=2))
for r in summary['hardware']:
    print(r)
