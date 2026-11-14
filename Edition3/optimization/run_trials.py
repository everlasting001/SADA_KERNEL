#!/usr/bin/env python3
"""Run isolated, reproducible Edition3 HLS experiments on Windows tools."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import time
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
EDITION = HERE.parent
WORK_ROOT = Path('/mnt/c/fpga/sada_edition3_runs')
CMD = '/mnt/c/Windows/System32/cmd.exe'
HLS = r'V:\Xilinx\Vitis_HLS\2022.2\bin\vitis_hls.bat'

PROFILES = {
    '00_norm_off': dict(norm='off'),
    '01_norm_ii8': dict(norm='8'),
    '02_norm_ii16': dict(norm='16'),
    '03_selection_serial': dict(norm='off', selection_serial=True),
    '04_density_serial': dict(norm='off', density_serial=True),
    '05_area_shared': dict(norm='off', density_serial=True, selection_serial=True),
    '06_key_cache': dict(norm='off', density_serial=True, selection_serial=True, key_cache=True),
    '07_balanced': dict(norm='8', density_serial=True, selection_serial=True, key_cache=True),
    '08_banked_cache': dict(norm='off', density_serial=True, selection_serial=True,
                           key_cache=True, bank_references=True),
    '09_top4_insert': dict(norm='off', density_serial=True, selection_serial=True,
                          insertion_top4=True),
    '12_bram_cache': dict(norm='off', density_serial=True, selection_serial=True,
                         key_cache='bram', bank_references=True),
    '13_insert_bram': dict(norm='off', density_serial=True, selection_serial=True,
                          insertion_top4=True, stable_ties=True, key_cache='bram', bank_references=True),
    '14_fast_insert_bram': dict(norm='8', density_serial=True, selection_serial=True,
                               insertion_top4=True, stable_ties=True, key_cache='bram', bank_references=True),
    '15_stable_top4': dict(norm='off', density_serial=True, selection_serial=True,
                          insertion_top4=True, stable_ties=True),
    '16_balanced_ii16': dict(norm='16', density_serial=True, selection_serial=True,
                            insertion_top4=True, stable_ties=True, key_cache='bram', bank_references=True),
    '17_balanced_ii32': dict(norm='32', density_serial=True, selection_serial=True,
                            insertion_top4=True, stable_ties=True, key_cache='bram', bank_references=True),
    '18_reference32': dict(snapshot='reference32_snapshot'),
    '19_raw_density16': dict(snapshot='no_normalization16_snapshot'),
    '20_lut_constant256': dict(snapshot='lut_constant256_snapshot'),
    '21_lut_linear64': dict(snapshot='lut_linear64_snapshot'),
    '22_lut_linear256': dict(snapshot='lut_linear256_snapshot'),
    '23_lut_linear1024': dict(snapshot='lut_linear1024_snapshot'),
    '24_lut_exact_control': dict(snapshot='lut_exact_control_snapshot'),
}


def function_span(text, name):
    start = text.index('void ' + name + '(')
    body = text.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return start, end


def edit_function(text, name, transform):
    start, end = function_span(text, name)
    return text[:start] + transform(text[start:end]) + text[end:]


def loop_pragma(text, name, pragma):
    def transform(body):
        target = 'for (int point = 0; point < POINT_COUNT; ++point) {'
        assert target in body
        return body.replace(target, target + '\n#pragma HLS ' + pragma, 1)
    return edit_function(text, name, transform)


def source_for(profile):
    options = PROFILES[profile]
    if options.get('snapshot'):
        return (HERE / options['snapshot'] / 'src/sada_kernel.cpp').read_text()
    source = (HERE / 'initial_snapshot/src/sada_kernel.cpp').read_text()

    def normalize(body):
        body = '\n'.join(line for line in body.split('\n')
                         if '#pragma HLS PIPELINE' not in line)
        target = 'for (int point = 0; point < POINT_COUNT; ++point) {'
        directive = 'PIPELINE off' if options['norm'] == 'off' else 'PIPELINE II=' + options['norm']
        return body.replace(target, target + '\n#pragma HLS ' + directive, 1)

    source = edit_function(source, 'normalize_clip_density', normalize)
    if options.get('density_serial'):
        source = loop_pragma(source, 'compute_raw_density', 'PIPELINE off')
    if options.get('selection_serial'):
        source = loop_pragma(source, 'select_density_references', 'PIPELINE off')
    if options.get('insertion_top4'):
        def insertion_top4(body):
            opening = body.index('{')
            result = body[:opening] + '''{
    density_score_t top_scores[DENSITY_EXTRA_COUNT];
    point_index_t top_ids[DENSITY_EXTRA_COUNT];
    bool valid[DENSITY_EXTRA_COUNT];
#pragma HLS ARRAY_PARTITION variable=top_scores complete dim=1
#pragma HLS ARRAY_PARTITION variable=top_ids complete dim=1
#pragma HLS ARRAY_PARTITION variable=valid complete dim=1
    for (int slot = 0; slot < DENSITY_EXTRA_COUNT; ++slot) {
#pragma HLS UNROLL
        top_scores[slot] = 0;
        top_ids[slot] = 0;
        valid[slot] = false;
    }
    for (int neighbor = 0; neighbor < NEAREST_COUNT; ++neighbor) {
        reference_indices[neighbor] = candidate_indices[neighbor];
    }
    for (int neighbor = NEAREST_COUNT; neighbor < CANDIDATE_COUNT; ++neighbor) {
#pragma HLS PIPELINE II=1
        point_index_t candidate = candidate_indices[neighbor];
        density_difference_t difference =
            density_difference_t(clipped_density[current_point]) -
            density_difference_t(clipped_density[candidate]);
        density_score_t carry_score;
        if (difference < 0) carry_score = density_score_t(-difference);
        else carry_score = density_score_t(difference);
        point_index_t carry_id = candidate;
        bool carry_valid = true;
        for (int rank = 0; rank < DENSITY_EXTRA_COUNT; ++rank) {
#pragma HLS UNROLL
            // Strict comparison keeps earlier candidate IDs first on ties.
            if (carry_valid && (!valid[rank] || carry_score > top_scores[rank])) {
                density_score_t previous_score = top_scores[rank];
                point_index_t previous_id = top_ids[rank];
                bool previous_valid = valid[rank];
                top_scores[rank] = carry_score;
                top_ids[rank] = carry_id;
                valid[rank] = true;
                carry_score = previous_score;
                carry_id = previous_id;
                carry_valid = previous_valid;
            }
        }
    }
    for (int rank = 0; rank < DENSITY_EXTRA_COUNT; ++rank) {
        reference_indices[NEAREST_COUNT + rank] = top_ids[rank];
    }
}'''
            if options.get('stable_ties'):
                result = result.replace('bool valid[DENSITY_EXTRA_COUNT];',
                    'bool valid[DENSITY_EXTRA_COUNT];\n    ap_uint<4> top_positions[DENSITY_EXTRA_COUNT];\n#pragma HLS ARRAY_PARTITION variable=top_positions complete dim=1')
                result = result.replace('valid[slot] = false;', 'valid[slot] = false;\n        top_positions[slot] = 0;')
                result = result.replace('point_index_t carry_id = candidate;',
                    'point_index_t carry_id = candidate;\n        ap_uint<4> carry_position = neighbor;')
                result = result.replace('carry_score > top_scores[rank]))',
                    'carry_score > top_scores[rank] ||\n                (carry_score == top_scores[rank] && carry_position < top_positions[rank])))')
                result = result.replace('bool previous_valid = valid[rank];',
                    'bool previous_valid = valid[rank];\n                ap_uint<4> previous_position = top_positions[rank];')
                result = result.replace('valid[rank] = true;',
                    'valid[rank] = true;\n                top_positions[rank] = carry_position;')
                result = result.replace('carry_valid = previous_valid;',
                    'carry_valid = previous_valid;\n                carry_position = previous_position;')
            return result
        source = edit_function(source, 'select_density_neighbors', insertion_top4)
    if options.get('key_cache'):
        def cache(body):
            outer = 'for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {'
            cached = '''for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
#pragma HLS PIPELINE off
        spike_t cached_keys[POINT_COUNT];
#pragma HLS ARRAY_PARTITION variable=cached_keys complete dim=1
        for (int load = 0; load < POINT_COUNT; ++load) {
#pragma HLS PIPELINE II=1
            cached_keys[load] = key_spikes[channel][load];
        }'''
            if options['key_cache'] == 'bram':
                cached = '''for (int channel = 0; channel < CHANNEL_COUNT; ++channel) {
#pragma HLS PIPELINE off
        const int KEY_COPIES = (REFERENCE_COUNT + 1) / 2;
        spike_t cached_keys[KEY_COPIES][POINT_COUNT];
#pragma HLS ARRAY_PARTITION variable=cached_keys complete dim=1
#pragma HLS BIND_STORAGE variable=cached_keys type=ram_2p impl=bram
        for (int load = 0; load < POINT_COUNT; ++load) {
#pragma HLS PIPELINE II=1
            spike_t key = key_spikes[channel][load];
            for (int copy = 0; copy < KEY_COPIES; ++copy) {
#pragma HLS UNROLL
                cached_keys[copy][load] = key;
            }
        }'''
            assert outer in body
            body = body.replace(outer, cached, 1)
            target = 'for (int point = 0; point < POINT_COUNT; ++point) {'
            body = body.replace(target, target + '\n#pragma HLS PIPELINE II=1', 1)
            assert 'key_spikes[channel][selected_point]' in body
            access = ('cached_keys[reference / 2][selected_point]'
                      if options['key_cache'] == 'bram' else 'cached_keys[selected_point]')
            return body.replace('key_spikes[channel][selected_point]', access)
        source = edit_function(source, 'apply_selected_attention', cache)
    if options.get('bank_references'):
        def bank_input(body):
            opening = body.index('{') + 1
            return body[:opening] + '\n#pragma HLS ARRAY_PARTITION variable=reference_indices complete dim=2' + body[opening:]
        source = edit_function(source, 'apply_selected_attention', bank_input)
        def bank_local(body):
            target = 'point_index_t reference_indices[POINT_COUNT][REFERENCE_COUNT];'
            assert target in body
            return body.replace(target, target + '\n#pragma HLS ARRAY_PARTITION variable=reference_indices complete dim=2', 1)
        source = edit_function(source, 'sada_attention_kernel', bank_local)
    return source


def winpath(path):
    return 'C:' + str(path.resolve()).removeprefix('/mnt/c')


def prepare(profile, experiment, stage, clock_ns=20):
    name = f'{profile}_exp{experiment}'
    if clock_ns != 20:
        name += '_clk' + format(clock_ns, 'g').replace('.', 'p')
    archive = HERE / 'trials' / name
    work = WORK_ROOT / name
    archive.mkdir(parents=True, exist_ok=True)
    (work / 'src').mkdir(parents=True, exist_ok=True)
    (work / 'test').mkdir(exist_ok=True)
    snapshot = PROFILES[profile].get('snapshot', 'initial_snapshot')
    for sub in ['src', 'test']:
        shutil.copytree(HERE / snapshot / sub, work / sub, dirs_exist_ok=True)
    (work / 'src/sada_kernel.cpp').write_text(source_for(profile))
    for sub in ['src', 'test']:
        shutil.copytree(work / sub, archive / sub, dirs_exist_ok=True)
    flags = f'-std=c++11 -I{winpath(work / "src")} -DSADA_EXPERIMENT={experiment}'
    tcl = f'''open_project -reset project
set_top sada_attention_kernel
add_files src/sada_kernel.cpp -cflags {{{flags}}}
add_files -tb test/tb_sada_experiments.cpp -cflags {{{flags}}}
open_solution solution1 -flow_target vivado
set_part {{xc7z010clg400-1}}
create_clock -period {clock_ns:g} -name default
csim_design
csynth_design
'''
    if stage in ['cosim', 'impl']:
        tcl += 'cosim_design -rtl verilog\n'
    if stage == 'impl':
        tcl += 'export_design -flow impl -rtl verilog -format ip_catalog\n'
    tcl += 'exit\n'
    (work / 'run_hls.tcl').write_text(tcl)
    (archive / 'run_hls.tcl').write_text(tcl)
    metadata = {
        'profile': profile, 'options': PROFILES[profile], 'experiment': experiment,
        'stage': stage, 'part': 'xc7z010clg400-1', 'clock_ns': clock_ns,
        'work_directory': winpath(work),
        'source_sha256': hashlib.sha256((work / 'src/sada_kernel.cpp').read_bytes()).hexdigest(),
        'header_sha256': hashlib.sha256((work / 'src/sada_kernel.h').read_bytes()).hexdigest(),
        'test_sha256': hashlib.sha256((work / 'test/tb_sada_experiments.cpp').read_bytes()).hexdigest(),
        'files_sha256': {str(path.relative_to(work)): hashlib.sha256(path.read_bytes()).hexdigest()
                         for sub in ['src', 'test'] for path in sorted((work / sub).rglob('*'))
                         if path.is_file()},
    }
    (archive / 'manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
    return work, archive, metadata


def collect(work, archive, metadata):
    solution = work / 'project/solution1'
    for folder in ['syn/report', 'sim/report', 'csim/report', 'impl/report']:
        origin = solution / folder
        if origin.exists():
            shutil.copytree(origin, archive / folder, dirs_exist_ok=True)
    exported_ip = solution / 'impl/export.zip'
    if metadata['stage'] == 'impl' and exported_ip.exists():
        (archive / 'impl').mkdir(exist_ok=True)
        shutil.copy2(exported_ip, archive / 'impl/export.zip')
    for filename in ['hls_console.log', 'vitis_hls.log']:
        if (work / filename).exists():
            shutil.copy2(work / filename, archive / filename)
    report = solution / 'syn/report/sada_attention_kernel_csynth.xml'
    result = dict(metadata)
    if report.exists():
        tree = ET.parse(report).getroot()
        result['resources'] = {item.tag: int(item.text)
                               for item in tree.find('AreaEstimates/Resources')}
        result['latency_cycles'] = int(tree.findtext('PerformanceEstimates/SummaryOfOverallLatency/Best-caseLatency'))
        result['interval_cycles'] = int(tree.findtext('PerformanceEstimates/SummaryOfOverallLatency/Interval-min'))
        result['estimated_clock_ns'] = float(tree.findtext('PerformanceEstimates/SummaryOfTimingAnalysis/EstimatedClockPeriod'))
    result['csim_pass'] = any('CSim done with 0 errors' in p.read_text(errors='replace')
                              for p in (archive / 'csim/report').glob('*.log'))
    result['cosim_pass'] = any('Pass' in p.read_text(errors='replace')
                               for p in (archive / 'sim/report').glob('*_cosim.rpt'))
    implementation = solution / 'impl/report/verilog/export_impl.rpt'
    if metadata['stage'] == 'impl' and implementation.exists():
        contents = implementation.read_text(errors='replace')
        result['implementation_timing_met'] = '* Timing was met' in contents
        result['implemented_resources'] = {
            key: int(re.search(r'^' + key + r':\s+(\d+)', contents, re.M).group(1))
            for key in ['LUT', 'FF', 'DSP', 'BRAM', 'SRL']
        }
        period = re.search(r'\| Post-Route\s*\|\s*([\d.]+)', contents)
        if period:
            result['post_route_period_ns'] = float(period.group(1))
        routed = solution / 'impl/verilog/project.runs/impl_1'
        for report_path in routed.glob('*.rpt'):
            destination = archive / 'impl/routed'
            destination.mkdir(parents=True, exist_ok=True)
            shutil.copy2(report_path, destination / report_path.name)
            if report_path.name.endswith('_power_routed.rpt'):
                power = report_path.read_text(errors='replace')
                result['power_w'] = {}
                for label, key in [('Total On-Chip Power (W)', 'total'),
                                   ('Dynamic (W)', 'dynamic'), ('Device Static (W)', 'static')]:
                    match = re.search(r'\| ' + re.escape(label) + r'\s*\|\s*([\d.]+)', power)
                    if match:
                        result['power_w'][key] = float(match.group(1))
    (archive / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('profile', choices=PROFILES)
    parser.add_argument('--experiment', type=int, choices=range(4), default=3)
    parser.add_argument('--stage', choices=['synth', 'cosim', 'impl'], default='synth')
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--clock', type=float, default=20)
    args = parser.parse_args()
    work, archive, metadata = prepare(args.profile, args.experiment, args.stage, args.clock)
    print('Prepared ' + winpath(work), flush=True)
    if args.prepare_only:
        return
    start = time.monotonic()
    with (work / 'hls_console.log').open('wb') as log:
        process = subprocess.run([CMD, '/d', '/c', HLS + ' -f run_hls.tcl'],
                                 cwd=work, stdout=log, stderr=subprocess.STDOUT)
    metadata['exit_code'] = process.returncode
    metadata['elapsed_seconds'] = round(time.monotonic() - start, 2)
    result = collect(work, archive, metadata)
    success = process.returncode == 0 and result['csim_pass'] and 'resources' in result
    if args.stage in ['cosim', 'impl']:
        success = success and result['cosim_pass']
    if args.stage == 'impl':
        success = success and result.get('implementation_timing_met', False)
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
