# Edition3 optimization runs

本目录保存实际工具运行记录，禁止把没有运行的 profile 当成结果。每次运行的源代码、编译参数、时钟、SHA-256、工具日志与报告独立归档。

**当前源码对应 `18_reference32`**：配置2为32参考点，配置0/1/3仍为8个；使用 `reference32_snapshot` 快照。原 `13_insert_bram` 及其余早期profile保留旧16参考点设置，不代表当前源码。

独立消融 `19_raw_density16` 使用 `no_normalization16_snapshot`：恢复16候选/配置2参考16点，删除密度统计、归一化和截断，直接比较48位原始密度差。其可读源码、测试和四组报告在 [ablation_no_normalization16](../ablation_no_normalization16/README.md)，不替换当前32点版。重建时使用 `python optimization/run_trials.py 19_raw_density16 --experiment N --stage impl`；不要对这一算法消融运行旧的归一化版等价回归脚本。

## Reproduce

环境：WSL Python 3；Windows Vitis HLS / Vivado 2022.2。默认工具位于 `V:\Xilinx`，构建目录为 `C:\fpga\sada_edition3_runs`。其他安装位置请修改 `run_trials.py` 的 `CMD`、`HLS`、`WORK_ROOT`；HLS通过安装环境调用Vivado。

在 `Edition3` 目录运行：

```bash
# Single experiment: C simulation, synthesis, RTL co-simulation.
python optimization/run_trials.py 18_reference32 --experiment 2 --stage cosim

# Include Vivado out-of-context synthesis, placement, routing and power reports.
python optimization/run_trials.py 18_reference32 --experiment 2 --stage impl

# Separate frequency exploration; does not overwrite the 20 ns trial.
python optimization/run_trials.py 18_reference32 --experiment 3 --stage impl --clock 15

# Generate source and Tcl without launching tools.
python optimization/run_trials.py 18_reference32 --experiment 0 --prepare-only

# Native regression: baseline vs optimized raw-bit trace, plus stable Top-4 oracle.
python optimization/check_equivalence.py --profile 18_reference32 --align-baseline-header
```

四种实验由 `--experiment 0` 至 `3` 分别构建。相同 profile 使用相同源文件，只有 `SADA_EXPERIMENT` 宏不同。不能同时启动同一 profile、配置和时钟的两次运行；重跑会更新该试验归档。需要永久保留新一轮结果时，新增 profile 名称。

`initial_snapshot` 是最初优化前的Edition3源码和测试，早期profile以它为基础。`18_reference32` 单独以 `reference32_snapshot` 为基础。当前 `Edition3/src` 不直接作为生成器输入，避免意外叠加修改；改动后需建立新的冻结快照。

32参考点回归的 `--align-baseline-header` 将未优化源码与新版头文件配对，使对照双方的参考预算和累加位宽相同。它验证32参考点时电路优化仍保持计算结果，不要求32参考点输出与旧16参考点相同。

`trials/<profile>_exp<N>[_clk15]/` 包含：

| 路径 | 内容 |
|---|---|
| `src/`, `test/` | 当次源码、头文件及原统一测试 |
| `run_hls.tcl`, `manifest.json` | 构建设置与源文件哈希 |
| `result.json` | 提取后的HLS资源、周期、验证结果；实现运行另有实现资源、时序、功耗 |
| `csim/report/` | C仿真 |
| `syn/report/` | HLS综合与分模块报告 |
| `sim/report/` | RTL协同仿真 |
| `impl/report/` | Vivado实现汇总 |
| `impl/routed/` | 布局布线后的功耗、利用率、时序等报告 |
| `impl/export.zip` | 对应配置导出的IP包；不是板级bitstream |
| `hls_console.log`, `vitis_hls.log` | 工具日志 |

完整Windows工程和检查点保留在构建目录；仓库只归档报告和重建所需文件。`result.json` 内的 `cosim_pass: false` 在 `stage: synth` 时表示**未运行协同仿真**，不能据此判定失败。09号试验是C仿真确实失败，禁止采用。

## Checks

原统一测试使用独立double参考模型检查密度选点和两步PLIF状态。新增回归包含：

- `test_top4_random.cpp`：1,000组独立稳定排序对照，其中200组全部同分。
- `test_equivalence_trace.cpp`：四种配置分别对照未优化版；4个样本×4步，覆盖随机/零/相同/小距离、随机Q/K/V、负初始膜电位及多种衰减参数。按原始位混合选点、累加、膜电位和输出，比较逐样本摘要。

摘要相等是回归证据，不是形式化等价证明。人工输入也不能替代真实点云与原PyTorch模型的精度验证。

## Measurement boundary

HLS资源与Vivado实现资源分开报告。BRAM统一按18 Kbit块计。时延是一个样本、一个时间步的内核时间，不包含PS控制、候选16NN、DDR传输和定点转换。

Vivado实现是 **out-of-context IP**，没有完整PS/AXI/DDR系统。外部数组存储不全计入片内资源；尤其32位膜电位本身为192 KiB，需要后续系统存储方案。

功耗为默认活动率的工具估计，没有SAIF，没有板级功率实测。`P × 内核时延`只能作为当前模型下的每次调用能量代理，不能称为整板能耗或完整推理能耗。
