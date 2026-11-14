#!/usr/bin/env python3
"""Freeze separate LUT trials without changing any existing kernel."""
import importlib.util
import json
from pathlib import Path
import shutil

HERE = Path(__file__).resolve().parent
EDITION = HERE.parent
spec = importlib.util.spec_from_file_location('analysis', HERE / 'analyze_data.py')
model = importlib.util.module_from_spec(spec)
spec.loader.exec_module(model)
DATA = Path(json.loads((HERE / 'data_analysis.json').read_text())['source_directory'])
dataset = model.load_dataset(DATA)
distances = [max(0, min((1 << 32) - 1, int(v * (1 << 20) + 0.5))) for v in dataset['group_dist']]
means = [sum(distances[i * 16:(i + 1) * 16]) // 16 for i in range(512)]
denominators = [(mean << 4) + 17 for mean in means]
exact = [(1 << 48) // d for d in denominators]
exact_ids = model.select(exact, dataset['idx_cand'])

VARIANTS = [('lut_constant256', 8, False), ('lut_linear64', 6, True),
            ('lut_linear256', 8, True), ('lut_linear1024', 10, True),
            ('lut_exact_control', 0, False)]

IMPLEMENTATION = '''density_t estimate_density(distance_t mean_distance) {
    const density_t epsilon = density_t(0.000001);
    density_t denominator = density_t(mean_distance) + epsilon;
    ap_uint<48> bits = denominator.range(47, 0);
    int exponent = 47 - bits.countLeadingZeros();
    ap_uint<48> normalized = bits << (47 - exponent);
    ap_uint<47> fraction = normalized.range(46, 0);
    ap_uint<LUT_INDEX_BITS> index = fraction >> (47 - LUT_INDEX_BITS);
#pragma HLS BIND_STORAGE variable=RECIPROCAL_TABLE type=rom_1p impl=bram
    ap_uint<64> word = RECIPROCAL_TABLE[index];
    ap_uint<32> reciprocal = word.range(63, 32);
#if LUT_LINEAR
    ap_uint<32 - LUT_INDEX_BITS> drop = word.range(31, 0);
    ap_uint<16> weight = fraction >> (47 - LUT_INDEX_BITS - 16);
    ap_uint<48> product = drop * weight;
    reciprocal -= ap_uint<32>((product + 32768) >> 16);
#endif
    ap_uint<64> result = reciprocal;
    if (exponent <= 17) {
        result <<= 17 - exponent;
    } else {
        int shift = exponent - 17;
        result = (result + (ap_uint<64>(1) << (shift - 1))) >> shift;
    }
    density_t output;
    output.range(47, 0) = result.range(47, 0);
    return output;
}'''


def main():
    baseline = EDITION / 'optimization/no_normalization16_snapshot'
    for name, bits, linear in VARIANTS:
        target = EDITION / 'optimization' / (name + '_snapshot')
        assert not target.exists(), target
        for folder in ['src', 'test']:
            shutil.copytree(baseline / folder, target / folder)
        config = ('#pragma once\n#define LUT_INDEX_BITS ' + str(bits) +
                  '\n#define LUT_LINEAR ' + str(int(linear)) +
                  '\n#define LUT_IS_EXACT ' + str(int(bits == 0)) + '\n')
        (target / 'src/reciprocal_config.h').write_text(config)
        source = (target / 'src/sada_kernel.cpp').read_text()
        source = source.replace('// Integer square root: 48 fractional input bits become 24 output bits.\n', '')
        if bits:
            entries = model.table(bits, linear)
            lines = ['#pragma once', '#include "reciprocal_config.h"',
                     'static const ap_uint<64> RECIPROCAL_TABLE[1 << LUT_INDEX_BITS] = {']
            lines += ['0x%016xULL,' % ((base << 32) | drop) for base, drop in entries]
            lines.append('};')
            (target / 'src/reciprocal_lut.h').write_text('\n'.join(lines) + '\n')
            source = source.replace('#include "sada_kernel.h"', '#include "sada_kernel.h"\n#include "reciprocal_lut.h"')
            start = source.index('density_t estimate_density(')
            end = source.index('\nvoid compute_raw_density(', start)
            source = source[:start] + IMPLEMENTATION + '\n' + source[end:]
        (target / 'src/sada_kernel.cpp').write_text(source)
        densities = [model.lut_density(d, bits, linear) for d in denominators] if bits else exact
        selected = model.select(densities, dataset['idx_cand'])
        fixture = (HERE / 'real_sample.h').read_text().replace('#endif', '')
        for label, values in [('REAL_VARIANT_DENSITY_BITS', densities),
                              ('REAL_VARIANT_IDS', sum(selected, [])),
                              ('REAL_EXACT_IDS', sum(exact_ids, []))]:
            fixture += 'static const std::uint64_t ' + label + '[] = {\n'
            for start in range(0, len(values), 16):
                fixture += ','.join(str(v) + 'ULL' for v in values[start:start + 16]) + ',\n'
            fixture += '};\n'
        fixture += '#endif\n'
        (target / 'test/real_sample.h').write_text(fixture)
        shutil.copy2(HERE / 'real_sample_test.h', target / 'test/real_sample_test.h')
        tb = (target / 'test/tb_sada_experiments.cpp').read_text()
        tb = tb.replace('int main() {', '#include "real_sample_test.h"\n\nint main() {')
        tb = tb.replace('    check_complete_kernel();', '    check_complete_kernel();\n    check_real_sample();')
        (target / 'test/tb_sada_experiments.cpp').write_text(tb)
        print('Prepared', target.name)


if __name__ == '__main__':
    main()
