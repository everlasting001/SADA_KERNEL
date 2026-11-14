#!/usr/bin/env python3
"""Inspect NPY exports and evaluate full-domain normalized reciprocal tables."""
import argparse
import array
import ast
import hashlib
import json
import math
from pathlib import Path
import struct

HERE = Path(__file__).resolve().parent


def read_npy(path):
    with path.open('rb') as stream:
        assert stream.read(6) == b'\x93NUMPY'
        version = stream.read(2)
        assert version == b'\x01\x00'
        length = struct.unpack('<H', stream.read(2))[0]
        meta = ast.literal_eval(stream.read(length).decode())
        assert not meta['fortran_order']
        values = array.array({'<f4': 'f', '<i8': 'q'}[meta['descr']])
        values.frombytes(stream.read())
    return meta, values


def nearest(numerator, denominator):
    return (2 * numerator + denominator) // (2 * denominator)


def table(bits, linear):
    size = 1 << bits
    if linear:
        endpoints = [nearest((1 << 31) * size, size + i) for i in range(size + 1)]
        return [(endpoints[i], endpoints[i] - endpoints[i + 1]) for i in range(size)]
    return [(nearest((1 << 31) * 2 * size, 2 * size + 2 * i + 1), 0) for i in range(size)]


def lut_density(denominator, bits, linear):
    exponent = denominator.bit_length() - 1
    normalized = denominator << (47 - exponent)
    fraction = normalized & ((1 << 47) - 1)
    index = fraction >> (47 - bits)
    base, drop = table(bits, linear)[index]
    if linear:
        weight = (fraction >> (47 - bits - 16)) & 65535
        base -= (drop * weight + 32768) >> 16
    if exponent <= 17:
        return base << (17 - exponent)
    return nearest(base, 1 << (exponent - 17))


def select(densities, candidates):
    result = []
    for point in range(512):
        row = candidates[point * 16:(point + 1) * 16]
        positions = sorted(range(4, 16), key=lambda slot: -abs(densities[point] - densities[row[slot]]))
        result.append(list(row[:4]) + [row[slot] for slot in positions[:4]])
    return result


def load_dataset(directory):
    result = {}
    for name in ['group_dist', 'idx_cand', 'x_q', 'x_k', 'x_v']:
        meta, values = read_npy(directory / (name + '.npy'))
        result[name] = values
        result[name + '_meta'] = meta
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    data = load_dataset(args.directory)
    d, ids = data['group_dist'], data['idx_cand']
    assert data['group_dist_meta']['shape'] == (1, 512, 16)
    assert data['idx_cand_meta']['shape'] == (1, 512, 16)
    assert all(math.isfinite(value) and value >= 0 for value in d)
    for point in range(512):
        row = ids[point * 16:(point + 1) * 16]
        assert len(set(row)) == 16 and point not in row
        assert all(0 <= value < 512 for value in row)
        assert all(d[point * 16 + slot] <= d[point * 16 + slot + 1] for slot in range(15))
    for name in ['x_q', 'x_k', 'x_v']:
        assert data[name + '_meta']['shape'] == (4, 1, 64, 512)
        assert all(value in (0, 1) for value in data[name])
    distances = [max(0, min((1 << 32) - 1, math.floor(value * (1 << 20) + 0.5))) for value in d]
    # ap_fixed division truncates its intermediate before the AP_RND cast.
    means = [sum(distances[i * 16:(i + 1) * 16]) // 16 for i in range(512)]
    denominators = [(mean << 4) + 17 for mean in means]
    exact = [(1 << 48) // denominator for denominator in denominators]
    expected = select(exact, ids)
    analysis = {'source_directory': str(args.directory), 'geometry_points': 512,
                'candidate_count': 16, 'real_channels': 64, 'real_time_steps': 4,
                'synthesis_channels': 96, 'epsilon_q24': 17,
                'input_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in sorted(args.directory.glob('*.npy'))},
                'mean_distance_range': [min(means) / (1 << 20), max(means) / (1 << 20)],
                'exact_density_range': [min(exact) / (1 << 24), max(exact) / (1 << 24)],
                'plif_parameters': {'decay': 0.5, 'threshold': 1, 'trained_parameters_available': False},
                'variants': []}
    for linear in [False, True]:
        for bits in [4, 5, 6, 7, 8, 9, 10]:
            approximate = [lut_density(value, bits, linear) for value in denominators]
            selected = select(approximate, ids)
            changed_order = sum(a != b for a, b in zip(expected, selected))
            changed_set = sum(set(a) != set(b) for a, b in zip(expected, selected))
            mismatch_sum = 0
            for step in range(4):
                for channel in range(64):
                    offset = (step * 64 + channel) * 512
                    for point in range(512):
                        query = int(data['x_q'][offset + point])
                        a = sum(query != data['x_k'][offset + j] for j in expected[point])
                        b = sum(query != data['x_k'][offset + j] for j in selected[point])
                        mismatch_sum += a != b
            item = {'linear': linear, 'index_bits': bits, 'entries': 1 << bits,
                    'max_relative_density_error': max(abs(a - b) / a for a, b in zip(exact, approximate)),
                    'changed_reference_order_rows': changed_order, 'changed_reference_sets': changed_set,
                    'changed_xor_sums': mismatch_sum, 'xor_comparisons': 4 * 64 * 512}
            analysis['variants'].append(item)
            print(item, flush=True)
    HERE.mkdir(parents=True, exist_ok=True)
    (HERE / 'data_analysis.json').write_text(json.dumps(analysis, indent=2) + '\n')
    # Integer fixtures are exact; no NumPy dependency or text-float parsing in C++.
    lines = ['#ifndef SADA_REAL_SAMPLE_H', '#define SADA_REAL_SAMPLE_H', '#include <cstdint>']
    for name, values, kind in [('REAL_DISTANCE_BITS', distances, 'unsigned'),
                               ('REAL_CANDIDATES', ids, 'unsigned'),
                               ('REAL_EXACT_DENSITY_BITS', exact, 'std::uint64_t')]:
        lines.append('static const ' + kind + ' ' + name + '[] = {')
        for start in range(0, len(values), 16):
            lines.append(','.join(str(value) + ('ULL' if kind == 'std::uint64_t' else '')
                                  for value in values[start:start + 16]) + ',')
        lines.append('};')
    for source, name in [('x_q', 'REAL_Q'), ('x_k', 'REAL_K'), ('x_v', 'REAL_V')]:
        words = [sum(int(data[source][start + bit]) << bit for bit in range(64))
                 for start in range(0, len(data[source]), 64)]
        lines.append('static const std::uint64_t ' + name + '[] = {')
        for start in range(0, len(words), 8):
            lines.append(','.join(hex(value) + 'ULL' for value in words[start:start + 8]) + ',')
        lines.append('};')
    lines.append('#endif')
    (HERE / 'real_sample.h').write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
