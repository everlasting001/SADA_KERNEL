#!/usr/bin/env python3
"""Export archived results; never infer measurements for unrun profiles."""
import csv
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
FIELDS = ['trial', 'experiment', 'clock_ns', 'stage', 'csim_pass', 'cosim_pass',
          'LUT', 'FF', 'DSP', 'BRAM_18K', 'latency_cycles', 'latency_ms',
          'interval_cycles', 'calls_per_second', 'implementation_timing_met',
          'impl_LUT', 'impl_FF', 'impl_DSP', 'impl_BRAM_18K',
          'post_route_period_ns', 'total_power_w', 'dynamic_power_w',
          'cosim_latency_cycles', 'dynamic_energy_proxy_uj', 'total_energy_proxy_uj']


def main():
    import re
    rows = []
    for path in sorted((HERE / 'trials').glob('*/result.json')):
        result = json.loads(path.read_text())
        row = {'trial': path.parent.name}
        for key in FIELDS:
            if key in result:
                row[key] = result[key]
        row.update(result.get('resources', {}))
        if 'latency_cycles' in result:
            row['latency_ms'] = result['latency_cycles'] * result['clock_ns'] / 1e6
            row['calls_per_second'] = 1e9 / (result['interval_cycles'] * result['clock_ns'])
        for key, value in result.get('implemented_resources', {}).items():
            row['impl_' + ('BRAM_18K' if key == 'BRAM' else key)] = value
        power = result.get('power_w', {})
        row['total_power_w'] = power.get('total', '')
        row['dynamic_power_w'] = power.get('dynamic', '')
        for report in (path.parent / 'sim/report').glob('*_cosim.rpt'):
            match = re.search(r'\|\s*Verilog\|\s*Pass\|\s*(\d+)', report.read_text())
            if match:
                row['cosim_latency_cycles'] = int(match.group(1))
        if power and 'cosim_latency_cycles' in row:
            time_to_uj = row['cosim_latency_cycles'] * result['clock_ns'] / 1000
            row['dynamic_energy_proxy_uj'] = power['dynamic'] * time_to_uj
            row['total_energy_proxy_uj'] = power['total'] * time_to_uj
        rows.append({key: row.get(key, '') for key in FIELDS})
    with (HERE / 'results.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    (HERE / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
    print('Exported', len(rows), 'archived trials.')


if __name__ == '__main__':
    main()
