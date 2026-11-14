#!/usr/bin/env python3
"""Compare deterministic raw-bit traces without running Windows HLS."""
import argparse
from pathlib import Path
import subprocess
import shutil
import tempfile

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--profile', default='13_insert_bram')
    parser.add_argument('--include', default='/mnt/v/Xilinx/Vitis_HLS/2022.2/include')
    parser.add_argument('--align-baseline-header', action='store_true',
                        help='Use the trial header for an unoptimized baseline with matching reference counts.')
    args = parser.parse_args()
    lines = []
    with tempfile.TemporaryDirectory(prefix='sada_equivalence_') as tmp:
        baseline = HERE / 'initial_snapshot/src'
        optimized = HERE / 'trials' / (args.profile + '_exp3') / 'src'
        if args.align_baseline_header:
            baseline = Path(tmp) / 'baseline_src'
            baseline.mkdir()
            shutil.copy2(HERE / 'initial_snapshot/src/sada_kernel.cpp', baseline / 'sada_kernel.cpp')
            shutil.copy2(optimized / 'sada_kernel.h', baseline / 'sada_kernel.h')
            lines.append('Baseline header aligned to trial; compare identical reference budgets, not 16 vs 32.')
        for experiment in range(4):
            traces = []
            for name, source in [('baseline', baseline), ('optimized', optimized)]:
                binary = Path(tmp) / (name + str(experiment))
                command = ['g++', '-std=c++11', '-O1', '-Wno-unknown-pragmas',
                           '-Wno-attributes', '-DSADA_EXPERIMENT=' + str(experiment),
                           '-I' + args.include, '-I' + str(source),
                           str(source / 'sada_kernel.cpp'),
                           str(HERE / 'test_equivalence_trace.cpp'),
                           '-Wl,-l:libgmp.so.10', '-o', str(binary)]
                subprocess.run(command, check=True, capture_output=True)
                trace = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout
                traces.append(trace)
            if traces[0] != traces[1]:
                raise RuntimeError('Trace mismatch for experiment ' + str(experiment))
            lines.append('PASS raw-bit trace equality: ' + args.profile + ' experiment=' + str(experiment))
            lines.append(traces[1].rstrip())
            print(lines[-2], flush=True)
        source = HERE / 'trials' / (args.profile + '_exp3') / 'src'
        binary = Path(tmp) / 'top4'
        subprocess.run(['g++', '-std=c++11', '-O1', '-Wno-unknown-pragmas', '-Wno-attributes',
                        '-DSADA_EXPERIMENT=3', '-I' + args.include, '-I' + str(source),
                        str(source / 'sada_kernel.cpp'), str(HERE / 'test_top4_random.cpp'),
                        '-Wl,-l:libgmp.so.10', '-o', str(binary)], check=True, capture_output=True)
        lines.append(subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout.rstrip())
    (HERE / ('equivalence_' + args.profile + '.log')).write_text('\n'.join(lines) + '\n')
    print(lines[-1])


if __name__ == '__main__':
    main()
