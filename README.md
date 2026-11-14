# SADA Kernel — HLS Experiments

面向 SADAFormer 的独立 **密度选择与脉冲 Attention 内核**。当前使用 Vitis HLS 2022.2，以 Zynq-7010 为首个目标器件。

**当前优化版本为 [Edition3](Edition3/README.md)，优化过程和结果见 [优化报告](Edition3/OPTIMIZATION_REPORT.md)。** Edition2保留为512点、96通道的四配置基线，以下原始资源表均明确属于Edition2；Edition2配置3的LUT估计超出7010容量。尚未完成板级系统部署。

**当前Edition3追加实验：配置2改为XOR＋32个参考点，配置0/1/3仍为8个。** 构建profile为 `18_reference32`；原 `13_insert_bram` 的配置2仍是16参考点历史版本。最新状态与数据见 [32参考点实验报告](Edition3/REFERENCE32_REPORT.md)，不要将旧16点报告对应到当前32点源码。

**另有独立的16候选消融：[去掉归一化与截断，直接比较原始密度差](Edition3/ablation_no_normalization16/README.md)。** 该组使用 `19_raw_density16`，配置2仍为16参考点，其余为8个。源码、测试与报告在独立目录中，没有替换上述32点版；资源对比见 [消融报告](Edition3/ablation_no_normalization16/REPORT.md)。

**查表倒数试验与真实样本验证：** [lut_reciprocal16](Edition3/lut_reciprocal16/README.md)，在上述原始密度消融版中比较精确定点除法、直接查表与查表插值。输入为用户导出的512点、4步、64通道样本；硬件综合保持96通道，差异统计只计实际64通道。见 [查表报告](Edition3/lut_reciprocal16/REPORT.md)。

**初步确定的多时间步方案：PS逐步调用PL。一次调用计算一个物体的一个时间步，膜电位在调用之间保留。** 当前HLS数组接口尚未接入PS/DDR，以下调度约定是接下来板级集成的目标，并非已经实现的驱动。

## 1. 目录与版本

```text
SADA_KERNEL/
├── README.md
├── Edition1/
│   ├── src/                 # 256-point initial density/XOR implementation
│   ├── test/                # Incremental module tests
│   └── report/              # Original full-kernel synthesis report
├── Edition2/
│   ├── README.md
│   ├── src/                 # 512-point four-configuration implementation
│   ├── test/
│   │   └── tb_sada_experiments.cpp
│   └── report/
│       ├── experiment_0/
│       ├── experiment_1/
│       ├── experiment_2/
│       └── experiment_3/
│           ├── csim/        # C simulation log
│           ├── syn/         # HLS synthesis reports
│           └── sim/         # RTL co-simulation reports
└── Edition3/
    ├── README.md
    ├── OPTIMIZATION_REPORT.md
    ├── src/                 # Selected implementation optimization
    ├── test/                # Unchanged correctness test, run on optimized source
    ├── report/              # Updated optimized HLS and simulation reports
    └── optimization/        # Trials, scripts, snapshots, logs and new reports
```

- [Edition2配置、类型及接口](Edition2/src/sada_kernel.h)
- [Edition3实现与复现说明](Edition3/README.md)
- [Edition3多轮优化报告](Edition3/OPTIMIZATION_REPORT.md)
- [Edition2实现](Edition2/src/sada_kernel.cpp)
- [Edition2统一测试](Edition2/test/tb_sada_experiments.cpp)
- [Edition2运行说明](Edition2/README.md)
- [Edition1实现](Edition1/src/sada_kernel.cpp)
- [Edition1综合报告](Edition1/report/sada_attention_kernel_csynth.rpt)
- [任务说明](../../temp.txt)：其中早期进度描述不代表本目录当前状态。
- [SADAFormer软件源码](../../SADAFormer/SADAFormer/models/point_pn.py)

### Edition3的src、test、report分别是什么版本？

**当前源码为 `18_reference32`：在已优化电路上，仅将配置2参考预算扩大为32。** 统一测试追加了最大累加值检查；`report/`已发布四组最新报告，全部通过C仿真、Verilog协同仿真及Vivado独立IP布局布线与时序检查。完整构建归档保留在 `optimization/trials/`，结果分析见 [追加报告](Edition3/REFERENCE32_REPORT.md)。

| 路径 | 当前内容 | 如何使用 |
|---|---|---|
| [Edition3/src/sada_kernel.cpp](Edition3/src/sada_kernel.cpp) | 保留原优化实现，候选位置类型适配32个候选 | 用于四种配置分别编译、仿真和综合 |
| [Edition3/src/sada_kernel.h](Edition3/src/sada_kernel.h) | 配置2改为32候选/32参考、7位累加；其他配置仍16候选/8参考、6位累加 | 仍为512点、96通道；PLIF状态和参数格式不变 |
| [Edition3/test/tb_sada_experiments.cpp](Edition3/test/tb_sada_experiments.cpp) | 保留独立参考模型，追加最大XOR累加及PLIF输入检查 | 验证32个不匹配得到+32，不能发生有符号溢出 |
| [Edition3/report/](Edition3/report/) | 最新四配置综合及仿真报告入口 | 日常查看 `experiment_N/syn/`、`csim/`、`sim/`；版本状态见32参考点报告 |
| [Edition3/optimization/initial_snapshot/](Edition3/optimization/initial_snapshot/) | 本轮开始前的源码、头文件及统一测试快照 | 核对优化前后的变化 |
| [Edition3/optimization/trials/](Edition3/optimization/trials/) | 多轮试验的源码快照、测试、参数、日志及新报告 | 按试验名称和配置编号读取，不能混用不同试验 |
| [Edition3/optimization/test_top4_random.cpp](Edition3/optimization/test_top4_random.cpp) | 新增的Top-4随机正确性测试 | 以独立稳定排序检查同分顺序 |
| [Edition3/optimization/test_equivalence_trace.cpp](Edition3/optimization/test_equivalence_trace.cpp) | 新增的优化前后原始位摘要回归测试 | 由脚本分别构建优化前后版本，对照四种配置 |

测试用于检查实现结果。扩展32参考点后，测试参考模型也按32个实际编号计算，不能继续期望原16参考点输出；其余三组的数学定义不变。新增回归测试放在 `optimization/`。每次构建只添加一个包含 `main()` 的测试文件，避免重复定义。

**当前四配置均采用 `18_reference32`、20 ns目标周期（50 MHz）。** 日常报告入口为 [Edition3/report](Edition3/report/) 中的 `experiment_0` 至 `experiment_3`；下表是保留源码哈希与工具记录的试验归档，具体完成状态见追加报告：

| 配置 | 对应新报告目录 | 已完成验证 |
|---|---|---|
| 0：减法＋最近8点 | [18_reference32_exp0](Edition3/optimization/trials/18_reference32_exp0/) | C仿真、Verilog协同仿真、Vivado独立IP布局布线与时序检查均通过 |
| 1：XOR＋最近8点 | [18_reference32_exp1](Edition3/optimization/trials/18_reference32_exp1/) | 同上 |
| 2：XOR＋全部32点 | [18_reference32_exp2](Edition3/optimization/trials/18_reference32_exp2/) | 同上 |
| 3：XOR＋密度选择8点 | [18_reference32_exp3](Edition3/optimization/trials/18_reference32_exp3/) | 同上 |

各目录内：`src/`、`test/`是当次构建快照；`manifest.json`记录源码哈希与参数；`syn/report/`是HLS综合报告；`sim/report/`是RTL协同仿真报告；`impl/report/`、`impl/routed/`是Vivado实现和时序、功耗报告；`impl/export.zip`是导出的IP包，不能直接作为板级bitstream使用。历史 `13_insert_bram_expN` 继续保留，不覆盖为新32参考点结果。

已清理 `Edition3/report/experiment_3/syn/` 中确认残留的5组旧分模块报告（共10个rpt/xml文件）。四组 `syn/` 的报告文件名集合均已与默认优化试验归档核对一致；删除路径及文件哈希见[清理记录](Edition3/optimization/cleanup_legacy_reports.json)。独立试验归档继续保留完整构建与实现记录。

`13_insert_bram_exp3_clk15`为同一源码按15 ns约束重新构建的配置3提频试验；`14_fast_insert_bram_exp3`为另一套速度优先实现，**不是当前默认源码**。它们单独分析，不与上述四组50 MHz结果混作受控对比。其他试验也不一定完成全部验证，其中09号C仿真失败，不能采用。

本轮还对密度Top-4做了等价实现优化。因此，默认配置3报告的是**优化后的完整密度选择开销**；若论文要讨论原始选择器开销或Top-4优化本身的收益，应另列相应对照，不能仅用默认四组结果归因。完整数据与测量边界见[Edition3优化报告](Edition3/OPTIMIZATION_REPORT.md)。

优化也包含四组共用的K缓存、参考编号分bank和流水线调度，并非只修改了密度选择器。XOR、有符号减法和PLIF的计算表达式保持不变；论文应将其描述为统一实现原则下的完整方案对比。

## 2. 实现范围

```text
Common sorted 16NN IDs + squared distances + Q/K/V spikes
                            |
                 select_reference_points
          +-----------------+-------------------+
          |                 |                   |
       nearest8           all32          density-selected8
                                    mean distance -> reciprocal
                                    density mean / sample std
                                    normalize -> clip [-3,3]
                                    nearest4 + density Top4
          +-----------------+-------------------+
                            |
                 Gather reference-point K
                            |
                  Q-K or XOR -> reference sum
                            |
                    PLIF state update
                            |
                 Current-point V AND gate
```

当前Edition3配置2输入32NN，其他配置仍输入16NN；可从同一32NN结果取前16项供其他配置使用。配置3的密度估计、归一化、截断及Top-4均保留在PL内核中，候选范围仍是16点。

不包含FPS、EncP、生成Q/K/V的卷积与神经元、Attention末尾投影卷积和BN、残差、MLP或分类器。内核输出对应软件的 `x = torch.mul(x_v, w)`，不是整个 `Attention.forward()` 的最终输出。

## 3. 四组受控实验

在设计文件和测试文件中使用相同的 `-DSADA_EXPERIMENT=N`；不指定时默认3。

| N | 配置 | 参考数量 | 参考点策略 | 比较方式 |
|---|---|---:|---|---|
| 0 | `SubtractNearest8` | 8 | 公共候选前8个 | 有符号减法 `Q-K` |
| 1 | `XorNearest8` | 8 | 公共候选前8个 | XOR |
| 2 | `XorAll32` | 32 | 32个最近候选全部使用 | XOR |
| 3 | `XorDensity8` | 8 | 最近4个＋其余候选密度差异最大的4个 | XOR |

配置在编译时决定，每组分别综合；不是PS在一次运行中切换的模式。当前代码没有统一运行时模式控制端口。

### 控制变量

- 四组使用相同的点云、排除自身规则、距离排序、Q/K/V、PLIF参数和初始状态。当前配置2扩展为32NN，其余仍用同一32NN列表的前16项；配置3的密度定义不改成32邻域。若比较完整PS端开销，应统一生成32NN并计入公共预处理成本。
- 统一512点、96通道、20 ns目标周期、器件及接口/优化原则。
- 0与1隔离比较算子；1与2考察参考数量；1与3考察选择策略及其额外开销。
- 2与3用于观察减少参考交互后，完整内核能否覆盖密度选择成本；不能把这组的全部差异只归因于一个因素。
- 配置0/1/2不需要计算密度，不为公平而执行无用工作；配置3必须计入完整密度选择开销。
- 四组各自对照相应参考结果，不要求输出相同。共享固定PLIF参数便于受控硬件比较，不等于已验证四种网络配置的分类精度。
- 参数名称和职责相同，但当前配置2候选数组为32列，其余16列；HLS还可以消除配置0/1/2中未使用的距离端口。板级封装和传输计时需要另行统一，不能把参数名称相同当成接口宽度或实际搬运量相同。

## 4. 接口与数据形状

一次顶层调用没有显式B/T维度：**一个物体、一个时间步**。

| 参数 | 形状 | 类型 | 含义 |
|---|---|---|---|
| `squared_distances` | `[512][CANDIDATE_COUNT]` | `distance_t` | 当前点到候选点的平方距离；配置2为32列，其余16列 |
| `candidate_indices` | `[512][CANDIDATE_COUNT]` | `point_index_t` | 与距离配对的候选点编号 |
| `query_spikes`, `key_spikes`, `value_spikes` | 各 `[96][512]` | `spike_t` | 当前时间步Q/K/V |
| `decay` | 标量 | `membrane_t` | `lambda=1/tau` |
| `threshold` | 标量 | `membrane_t` | 发放阈值 |
| `membrane` | `[96][512]` | `membrane_t` | 读旧状态、写新状态 |
| `gated_values` | `[96][512]` | `spike_t` | 当前步门控后的V |
| `attention_sums` | `[96][512]` | `attention_sum_t` | 比较累加值，调试输出 |

`candidate_indices[i][r]` 是第i个当前点的第r个候选的实际编号j；`squared_distances[i][r]` 是i到j的平方距离。每行候选互不重复、排除自身，编号与距离同步按距离从近到远排列。编号范围为0..511；配置2的候选位置为0..31，其余配置为0..15。

静态坐标对应的候选与距离跨通道、跨时间步共用。Q/K/V可以随时间变化；不能未经软件验证就在各时间步重复使用相同脉冲。

## 5. 数值规则

### 密度分支

```text
d[i]       = mean(squared_distances[i][0..15])
rho[i]     = 1 / (d[i] + eps)
mu         = mean(rho[0..511])
variance   = sum((rho[i]-mu)^2) / 511
std_safe   = max(sqrt(variance), eps)
rho_clip[i]= clamp((rho[i]-mu)/std_safe, -3, 3)
```

样本方差除以 `N-1`，对应软件 `rho.std()` 的默认规则。`eps=1e-6` 在当前密度定点格式中约为 `1.01327896e-6`。整数逐位开方保留24个小数位，之后对标准差执行epsilon下限保护。

密度分支固定在16个候选中输出8个编号；配置2的32个参考点不会改变配置3的4+4规则。密度差异并列时，本实现优先候选位置靠前者，PyTorch `topk` 不保证相同规则。

### 比较、PLIF与门控

```text
sum = sum(Q[c,i] - K[c,j])              # Configuration 0
sum = sum(Q[c,i] XOR K[c,j])            # Configurations 1/2/3
h   = membrane[c,i] + decay*(sum-membrane[c,i])
gate = (h >= threshold)
membrane[c,i] = gate ? 0 : h
gated_values[c,i] = V[c,i] AND gate
```

PLIF使用 `decay_input=True`、`v_reset=0`。参数在同一个模块内共享，各点、各通道的膜电位独立。训练参数为w时，在软件端计算 `decay=sigmoid(w)` 后定点化；当前测试使用0.5和阈值1验证逻辑，未替代训练参数。

当前Edition3的 `attention_sum_t` 在配置2使用7位有符号格式，以表示 `[0,32]`；其他配置保持6位。原6位有符号数最大只能表示31，不能直接用于32次XOR累加。单次减法仍使用2位有符号操作数。V属于当前点，没有汇总参考点V。

### 定点格式

定点别名使用 `AP_RND` 和 `AP_SAT`；有符号格式的整数位包含符号位。

| 类型 | 格式 | 用途 |
|---|---|---|
| `spike_t` | `ap_uint<1>` | 二值脉冲 |
| `point_index_t` | `ap_uint<9>` | 点编号 |
| `spike_difference_t` | `ap_int<2>` | 单次有符号差 |
| `attention_sum_t` | 配置2 `ap_int<7>`，其余 `ap_int<6>` | 比较累加结果 |
| `membrane_t` | `ap_fixed<32,16>` | 状态及PLIF参数 |
| `distance_t` | `ap_ufixed<32,12>` | 平方距离，20个小数位 |
| `distance_sum_t` | 配置2 `ap_ufixed<37,17>`，其余 `ap_ufixed<36,16>` | 适配32/16项求和；配置2顶层不计算密度 |
| `density_t` | `ap_ufixed<48,24>` | 原始密度、均值、标准差 |
| `density_sum_t` | `ap_ufixed<57,33>` | 512项密度求和 |
| `density_offset_t` | `ap_fixed<49,25>` | 原始密度减均值 |
| `variance_t` | `ap_ufixed<96,48>` | 方差及平方差 |
| `variance_sum_t` | `ap_ufixed<105,57>` | 平方差求和 |
| `normalization_t` | `ap_fixed<72,48>` | 截断前归一化值 |
| `normalized_density_t` | `ap_fixed<27,3>` | 截断后的密度 |
| `density_difference_t` | `ap_fixed<28,4>` | 截断密度差，范围[-6,6] |
| `density_score_t` | `ap_ufixed<27,3>` | 差异绝对值，范围[0,6] |

这些是功能验证用保守格式，真实数据位宽与误差优化尚未完成。修改规模时要一起检查索引、累加器和开方硬编码位宽。

float32文件可以保留，但传给定点内核前需要数值转换；float32的32位编码不等于32位定点编码。当前距离的编码比例是 `2^20`，CPU预打包必须匹配HLS舍入、饱和和字节序。普通整数赋值表示数值转换，`.range()` 写入的是位模式。

## 6. 仿真、综合与报告

Vitis HLS 2022.2设置：

```text
Flow: Vivado IP Flow
Part: xc7z010clg400-1
Clock: 20 ns
Top: sada_attention_kernel
Design Source: Edition2/src/sada_kernel.cpp
Test Bench: Edition2/test/tb_sada_experiments.cpp
C flags for BOTH files:
    -std=c++11 -I<absolute_Edition2_src_path> -DSADA_EXPERIMENT=N
```

每次只启用一个带main的测试文件。头文件搜索路径须指向对应版本的src，避免编译到Edition1或其他副本。断言测试不要定义 `NDEBUG`。

运行顺序：C Simulation → C Synthesis → C/RTL Co-Simulation。四组分别编译和保存报告；版本、器件、时钟约束、优化设置保持一致。

当前目录保存了源文件、测试和报告；Edition3另提供[自动构建与优化试验脚本](Edition3/optimization/README.md)。尚没有完整可运行的PS程序、Vivado板级工程或AXI/DDR封装。

原始HLS报告一般位于 `<project>/<solution>/syn/report/`，归档后位于：

```text
Edition2/report/experiment_N/csim/sada_attention_kernel_csim.log
Edition2/report/experiment_N/syn/sada_attention_kernel_csynth.rpt
Edition2/report/experiment_N/syn/sada_attention_kernel_csynth.xml
Edition2/report/experiment_N/sim/sada_attention_kernel_cosim.rpt
```

### 四组基线：2026-11-14

均为512点、96通道、20 ns目标周期、5.40 ns时钟不确定度。归档的四组C仿真均为0错误，Verilog协同仿真均为Pass。

| 配置 | LUT | FF | DSP | BRAM_18K | HLS延迟/周期 | HLS延迟/ms | 顶层间隔/周期 |
|---|---:|---:|---:|---:|---:|---:|---:|
| 0：减法＋最近8点 | 1,128 | 384 | 3 | 2 | 200,712 | 4.01424 | 200,713 |
| 1：XOR＋最近8点 | 1,056 | 384 | 3 | 2 | 200,712 | 4.01424 | 200,713 |
| 2：XOR＋16点 | 1,402 | 399 | 3 | 5 | 401,416 | 8.02832 | 401,417 |
| 3：XOR＋密度选择8点 | **21,354** | 17,302 | 27 | 12 | 217,310 | 4.34620 | 217,311 |

对应综合报告：[配置0](Edition2/report/experiment_0/syn/sada_attention_kernel_csynth.rpt)、[配置1](Edition2/report/experiment_1/syn/sada_attention_kernel_csynth.rpt)、[配置2](Edition2/report/experiment_2/syn/sada_attention_kernel_csynth.rpt)、[配置3](Edition2/report/experiment_3/syn/sada_attention_kernel_csynth.rpt)。

协同仿真延迟单独记录，避免与综合估计混用：

| 配置 | Verilog状态 | 仿真延迟/周期 | 仿真间隔/周期 |
|---|---|---:|---:|
| 0 | Pass | 200,710 | 200,711 |
| 1 | Pass | 200,710 | 200,711 |
| 2 | Pass | 401,414 | 401,415 |
| 3 | Pass | 217,297 | 217,298 |

器件资源上限为17,600 LUT、35,200 FF、80 DSP、120个BRAM_18K。**配置3的LUT约为121%，不能作为已可部署版本；其他配置估计在资源范围内，也仍需完整系统实现验证。**

配置3的密度选择器为20,305 LUT、24 DSP、20,694周期。主要LUT来自归一化/截断7,143、原始密度5,548、参考选择循环5,606。后续脉冲计算模块约196,613周期。资源优化和延迟优化的重点并不相同。

当前观察：0换成1减少72个LUT，延迟相同；2换成1约减半延迟；3比1慢约8.3%，比2快约46%，但付出了很高的选择资源成本。这些是当前架构、位宽、端口及调度下的基线，不是能耗或分类精度结论。

### 报告边界

- 当前数组接口是 `ap_memory`。Q/K/V、距离、编号、膜电位和输出的外部存储与控制器未全部计入HLS内部BRAM。
- BRAM按18 Kbit块计数，不是36 Kbit块数。
- 延迟不包含CPU找16NN、定点打包、PS控制、DMA/DDR传输与结果读取。
- HLS估计频率不等于布局布线后频率；协同仿真Pass不等于资源能放下或已经上板。
- Edition1是256点基线，完整内核为21,317 LUT、108,765周期，不能直接当成512点版本对照。

## 7. 多时间步执行约定：PS逐步调用

### 初步选择

**由PS控制时间步，每次启动PL完成一个完整时间步，不拆成逐点或逐通道由PS调用。** 暂不在PL内部增加T循环。

```text
Sequence start
  PS: prepare shared candidates/distances; initialize membrane once
  for t = 0 .. T-1:
      PS: prepare Q[t], K[t], V[t] and configure buffers
      PS: start PL
      PL: select references -> compare -> PLIF -> gate V
      PL: write back membrane and output
      PS: wait for completion; synchronize/read data as needed
  Sequence end
```

当前HLS顶层在功能上已是单步接口，膜电位由调用者保存；但把这些数组映射到DDR并由PS启动，仍需要后续新增AXI/控制封装与驱动。

### 数据保存与状态生命周期

| 数据 | 初步安排 |
|---|---|
| 16NN编号、平方距离 | PS每个静态样本准备一次，保留在DDR，跨步复用同一数据 |
| Q/K/V | 每步提供对应脉冲；可按步搬入，也可预先存入DDR后切换步偏移 |
| 膜电位 `[96][512]` | 32位定点，数据量192 KiB；在序列开始初始化一次，跨步读写同一状态缓冲区 |
| PLIF参数 | 同一模块在各步使用相同训练参数 |
| 输出V | 每步写入输出缓冲；调试时逐步读取，不要求正式测量时都拷回PS |
| 累加调试输出 | 当前保留以定位问题；正式去除时四组必须一致，并重新验证、综合 |

不可每步清零膜电位，不可让上一步尚未完成时覆盖输入/状态。新的独立样本开始时，才重新初始化相应状态。PS需确保PL读到本步有效数据、写回结果可见；DDR缓存同步方法在确定裸机或Linux驱动后实现。

这里的192 KiB是单份状态缓冲的数据量，不代表已经计入HLS BRAM；若每步从DDR完整读写一次状态，仅状态载荷就约384 KiB/步，实际带宽和访存开销需要板级测量。

### 当前密度计算次数

同一静态点云的密度选择结果不随时间变化，但**当前每次调用顶层都会重新选择参考点**。PS逐步启动T次时，配置3的密度估计、归一化和Top-4也执行T次。

本轮先保留这个行为作为清晰的单步基线，不声称已经实现了跨步参考点缓存。若后续优化为“序列开始由PL选择一次、其余步复用参考编号”，需要新增准备/状态接口，并将一次选择的成本计入序列总成本，四组统一采用相同的序列计时边界。不能把密度转到PS后仍当作完整PL选择器结果。

### 为什么初期采用此方案

PS逐步调用便于打印每步输出、核对膜电位及定位首次错误的时间步，也便于接入当前已验证的单步函数。代价是每步启动、同步和可能的数据搬运开销。

后续如果测量显示这些开销显著，再评估PL内部一次性执行T步。PL内部跑T步不要求把全部时间步同时放进BRAM，可以从DDR流式读取；不过它需要新的调度、状态和数据接口，本版尚未实现。

### 计时原则

同时报告两个层次：

1. 单步/多步的PL内核时间；当前报告只给出了单步内核估计及RTL仿真周期。
2. PS可见的序列总时间，包括候选准备、定点转换、必要的数据搬运、T次启动/等待及输出读取，避免对重叠部分重复加总。

不能直接把单步HLS延迟乘T，称为完整推理实测时间。四组必须统一T、初始状态、每步Q/K/V、输出读取策略及缓存复用规则。四配置各自的位流/IP在序列运行前选择，不是每个时间步重新切换配置。

## 8. 测试覆盖与后续工作

当前统一测试检查索引高位、累加范围、负输入PLIF、密度差6、小方差、候选去重、选点与两步完整状态。参考选点使用独立double公式及稳定排序，PLIF使用普通数值参考；所选脉冲与状态使这些测试的PLIF运算可精确表示。

人工距离与候选不保证来自真实几何点云；参考密度使用定点epsilon。仍需真实16NN、训练后Q/K/V和PLIF参数，对照原始PyTorch，统计选点变化、近似并列和阈值翻转。当前测试通过不能证明模型分类精度。

Edition3已完成多轮实现优化及独立IP布局布线，详见[优化报告](Edition3/OPTIMIZATION_REPORT.md)。下一阶段：完成真实数据对照，设计PS逐步调用所需的AXI/DDR接口，在完整系统实现后上板测量时序、资源、吞吐率及能耗。工具功耗估计与上板实测分别报告。
