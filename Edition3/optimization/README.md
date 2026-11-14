# Edition3 optimization runs

本目录保存实际工具运行记录，禁止把没有运行的 profile 当成结果。每次运行的源代码、编译参数、时钟、SHA-256、工具日志与报告独立归档。

## Reproduce

环境：WSL Python 3；Windows Vitis HLS / Vivado 2022.2。默认工具位于 `V:\Xilinx`，构建目录为 `C:\fpga\sada_edition3_runs`。其他安装位置请修改 `run_trials.py` 的 `CMD`、`HLS`、`WORK_ROOT`；HLS通过安装环境调用Vivado。

在 `Edition3` 目录运行：

```bash
# Single experiment: C simulation, synthesis, RTL co-simulation.
python optimization/run_trials.py 13_insert_bram --experiment 3 --stage cosim

# Include Vivado out-of-context synthesis, placement, routing and power reports.
python optimization/run_trials.py 13_insert_bram --experiment 3 --stage impl

# Separate frequency exploration; does not overwrite the 20 ns trial.
python optimization/run_trials.py 13_insert_bram --experiment 3 --stage impl --clock 15

# Generate source and Tcl without launching tools.
python optimization/run_trials.py 13_insert_bram --experiment 0 --prepare-only

# Native regression: baseline vs optimized raw-bit trace, plus stable Top-4 oracle.
python optimization/check_equivalence.py --profile 13_insert_bram
```

四种实验由 `--experiment 0` 至 `3` 分别构建。相同 profile 使用相同源文件，只有 `SADA_EXPERIMENT` 宏不同。不能同时启动同一 profile、配置和时钟的两次运行；重跑会更新该试验归档。需要永久保留新一轮结果时，新增 profile 名称。

`initial_snapshot` 是本轮开始前的 Edition3 源码和测试；生成器始终以它为基础。优化后的 `Edition3/src` 不作为生成器输入，避免叠加修改。

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
