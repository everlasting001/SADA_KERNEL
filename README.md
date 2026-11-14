# SADA Kernel — HLS Implementation

面向 SADAFormer 的独立 **密度选择与脉冲 Attention 内核**。使用 Vitis HLS 2022.2，首个目标器件为 Zynq-7010。

当前版本保存在 `Edition1/`，优先验证完整功能链，再进行资源、精度和性能优化。

## 1. 当前进度

| 项目 | 当前状态 |
|---|---|
| 原始密度、均值、样本方差、标准差 | 已实现 |
| 密度归一化、截断到 `[-3,3]` | 已实现 |
| 最近4点＋密度差异Top-4 | 已实现 |
| 参考K读取、XOR累加、PLIF、V门控 | 已实现并连接为顶层 |
| 分模块与端到端测试 | 已提供测试文件 |
| 完整内核HLS综合 | 已归档第一版 `.rpt` / `.xml` |
| 7010完整系统实现与上板 | 尚未完成；V1 LUT估计超出器件容量 |
| 真实点云、训练参数与PyTorch逐层对照 | 待完成 |
| 四组配置的统一对比 | 待补齐 |

目录中没有归档全部 C Simulation / Co-Simulation 运行日志。测试的具体通过状态应以重新运行的日志为准，不能仅凭存在测试文件或综合报告判断。

## 2. 目录

```text
SADA_KERNEL/
├── README.md
└── Edition1/
    ├── src/
    │   ├── sada_kernel.h       # Configuration, types, interfaces
    │   └── sada_kernel.cpp     # Kernel implementation
    ├── test/
    │   └── tb_*.cpp            # One main() per test file
    └── report/
        ├── sada_attention_kernel_csynth.rpt
        └── sada_attention_kernel_csynth.xml
```

- [配置与接口](Edition1/src/sada_kernel.h)
- [内核实现](Edition1/src/sada_kernel.cpp)
- [完整内核测试](Edition1/test/tb_sada_attention_kernel.cpp)
- [V1综合文本报告](Edition1/report/sada_attention_kernel_csynth.rpt)
- [V1综合XML报告](Edition1/report/sada_attention_kernel_csynth.xml)
- [任务说明](../../temp.txt)：实现范围和四组实验要求；其中早期进度文字不代表本目录当前进度。
- [SADAFormer软件源码](../../SADAFormer/SADAFormer/models/point_pn.py)

## 3. 实现边界

内核接收已经准备好的16NN候选和Q/K/V脉冲，输出门控后的V。

```text
Sorted 16NN IDs + squared distances
                |
        Mean squared distance
                |
       Raw density: 1/(d+eps)
                |
     Per-sample mean and sample std
                |
       Normalize and clip [-3,3]
                |
    Nearest4 + density-difference Top4
                |
         8 reference point IDs
                |
Q + gathered reference K -> XOR -> sum over references
                                      |
                                     PLIF
                                      |
Current-point V --------------------> AND -> gated V
```

首版允许CPU完成16NN查找。CPU必须提供配对且按距离排序的候选编号与平方距离，并排除当前点自身。

以下部分不在当前内核中：

- FPS、局部邻域特征提取和整个 EncP。
- 生成Q/K/V的卷积、BN与脉冲神经元。
- `Attention.forward()` 末尾的投影卷积和BN。
- Transformer残差、MLP和分类器。

软件对照位置应取 `x = torch.mul(x_v, w)` 的结果，而非整个 `Attention.forward()` 的最终输出。

## 4. 当前规模与顶层数据

```cpp
N_POINTS   = 256;
N_CHANNELS = 96;
N_CAND     = 16;
N_REFS     = 8;
N_NEAR     = 4;
N_DENSITY  = 4;
```

该规模是开发用配置，不等于原模型某个阶段的完整复现。一个顶层调用处理 **一个物体、一个时间步**，不含显式B/T数组维度。头文件中的 `TIME_STEPS=1` 不会让顶层自动循环时间步。

顶层函数：`sada_attention_kernel()`。

| 参数 | 形状 | 类型 | 含义 |
|---|---|---|---|
| `distances` | `[256][16]` | `distance_t` | 当前点到16个候选的平方距离 |
| `candidate_idx` | `[256][16]` | `index_t` | 与距离逐项对应的实际点编号 |
| `q`, `k`, `v` | 各 `[96][256]` | `spike_t` | 按 `[channel][point]` 排列的二值脉冲 |
| `lambda` | 标量 | `fixed_t` | PLIF衰减系数 `1/tau` |
| `threshold` | 标量 | `fixed_t` | 发放阈值 |
| `membrane` | `[96][256]` | `fixed_t` | 输入旧状态，写回新状态 |
| `output` | `[96][256]` | `spike_t` | 门控后的当前点V |
| `mismatch_count` | `[96][256]` | `count_t` | XOR差异计数，调试输出 |

### 编号与距离的对应关系

```cpp
candidate_idx[i][r]  // Actual point ID j, not candidate position r.
distances[i][r]      // Squared distance from point i to point j.
```

- `i` 是当前点，范围 `0..255`。
- `r` 是候选位置，范围 `0..15`。
- 候选数组中的值 `j` 是点编号，范围 `0..255`。
- 每行16个候选应互不重复，且不包含 `i`。
- 距离是已经计算好的平方距离，不要再次平方或开平方。
- 同一静态点云的候选与距离可以跨时间步共用。当前顶层每次调用仍会重新计算密度选择，尚未做跨调用缓存。

## 5. 计算规则

### 密度与归一化

对每个点 `i`：

```text
d[i]       = mean(distances[i][0..15])
rho[i]     = 1 / (d[i] + eps)
mu         = mean(rho[0..255])
variance   = sum((rho[i]-mu)^2) / (N_POINTS-1)
std_safe   = max(sqrt(variance), eps)
rho_clip[i]= clamp((rho[i]-mu)/std_safe, -3, 3)
```

`eps=1e-6`，在当前 `density_t` 格式下约为 `1.01327896e-6`。密度均值和标准差在一个物体内部统计，不跨物体统计。方差采用样本方差的 `N-1` 分母，与源码 `rho.std()` 默认规则一致。

`sqrt_variance()` 使用整数逐位开平方：读取方差的96位编码，生成48位根编码，对应标准差的24个小数位。该实现截断根的小数尾部，不是浮点开平方；最终还要执行epsilon下限保护。

### 参考点选择

1. 原样保留候选位置 `0..3` 的编号。
2. 对位置 `4..15` 计算 `abs(rho_clip[i]-rho_clip[j])`。
3. 依次取最大且未使用的候选，共4个。
4. 将两组编号拼接为8个参考。

本实现并列分数优先选择候选位置靠前的点。PyTorch `topk` 不保证使用相同的并列规则；真实数据对照时需要明确并列处理及比较方式。

### 脉冲计算与PLIF

对当前通道 `c`、当前点 `i`：

```text
count = sum(Q[c,i] XOR K[c,active_idx[i,r]]), r=0..7
h     = membrane[c,i] + lambda*(count-membrane[c,i])
gate  = (h >= threshold)
membrane[c,i] = gate ? 0 : h
output[c,i]   = V[c,i] AND gate
```

这对应 `decay_input=True`、`v_reset=0` 的PLIF。一个模块共享 `lambda` 和阈值，各通道、各点的膜电位独立。

- 独立推理序列开始前初始化膜电位，通常为零。
- 同一序列的时间步之间保留膜电位；函数内部不清空整张状态数组。
- 训练参数为可学习的 `w` 时，应在软件端计算 `lambda=sigmoid(w)`，再定点化后传入。
- 测试中的 `lambda=0.5`、阈值1及部分非零初始膜电位用于验证逻辑，尚不能替代训练模型参数。
- 参考点提供的是K；门控作用于当前点自己的V，不是对参考点V加权求和。

`attention_point/channel/frame()` 是开发过程中的最近8点路径；完整密度配置使用 `sada_attention_kernel()`，由它读取 `active_idx`，不能误接旧路径。

## 6. 定点格式与外部存储

所有下列定点别名均使用 `AP_RND` 舍入和 `AP_SAT` 饱和模式；有符号类型的整数位数包括符号位。

| 类型 | 格式 | 用途 |
|---|---|---|
| `spike_t` | `ap_uint<1>` | Q/K/V、门控输出 |
| `index_t` | `ap_uint<8>` | 256个点的编号 |
| `count_t` | `ap_uint<5>` | 无符号XOR计数 |
| `fixed_t` | `ap_fixed<32,16>` | PLIF参数、膜电位 |
| `distance_t` | `ap_ufixed<32,12>` | 平方距离，20个小数位 |
| `distance_sum_t` | `ap_ufixed<36,16>` | 16个距离累加 |
| `density_t` | `ap_ufixed<48,24>` | 原始密度、均值、标准差 |
| `density_sum_t` | `ap_ufixed<56,32>` | 256个密度累加 |
| `density_diff_t` | `ap_fixed<49,25>` | 原始密度减均值 |
| `variance_t` | `ap_ufixed<96,48>` | 方差、平方差 |
| `variance_sum_t` | `ap_ufixed<104,56>` | 平方差累加 |
| `normalization_work_t` | `ap_fixed<72,48>` | 截断前归一化值 |
| `normalized_density_t` | `ap_fixed<27,3>` | 截断密度 |
| `density_delta_t` | `ap_fixed<28,4>` | 截断密度差，范围 `[-6,6]` |
| `density_score_t` | `ap_ufixed<27,3>` | 差异绝对值，范围 `[0,6]` |

这是功能验证用的保守格式，尚未完成真实数据上的位宽与误差优化。改变 `N_POINTS` 时，也要检查编号位宽、累加器余量、测试常量，以及平方根函数中硬编码的位宽。

### float32文件不等于定点编码

距离文件可以保存为float32，但必须做数值转换后再输入定点内核。将float32的32位原始编码直接当成 `distance_t` 会得到错误数值。

```cpp
float value = 0.25f;
distance_t converted = distance_t(value);  // Numeric conversion.
```

CPU侧预打包时，当前距离编码等价于按HLS规则对 `value*2^20` 舍入并饱和，再传输编码。直接设置HLS对象的 `.range()` 表示写入位模式；普通整数赋值则表示数值转换，两者不能混用。当前版本尚未提供二进制文件转换器或板端传输程序。

## 7. 在 Vitis HLS 2022.2 中运行

完整内核的设置：

| 设置 | 值 |
|---|---|
| Flow | Vivado IP Flow |
| Target device | `xc7z010clg400-1`，报告显示为 `xc7z010-clg400-1` |
| Clock period | `20 ns`，50 MHz |
| Top Function | `sada_attention_kernel` |
| Design Source | `Edition1/src/sada_kernel.cpp` |
| Header | `Edition1/src/sada_kernel.h` |
| Test Bench | `Edition1/test/tb_sada_attention_kernel.cpp` |

测试与头文件现在分属不同目录，需要给Test Bench的编译选项设置头文件搜索路径，例如：

```text
-IC:/your_path/SADA_KERNEL/Edition1/src
```

使用自己电脑上的实际绝对路径；也可以将 `-I` 配置为合适的相对路径。仅把头文件列入项目不应作为编译器已经配置搜索路径的依据。

运行顺序：

1. **C Simulation**：验证C++逻辑与断言。
2. **C Synthesis**：生成RTL，检查HLS时序、延迟和资源估计。
3. **C/RTL Co-Simulation**：用同一测试程序验证生成的RTL行为。
4. 功能验证后，再做接口集成、Vivado实现与上板。

每次只启用一个带 `main()` 的测试文件，其他测试从本轮工程列表中移除，磁盘文件保留。测试文件不得混入Design Sources，也不要通过 `#include` 包含其他测试 `.cpp`。运行断言检查时不要定义 `NDEBUG`。

HLS报告默认位置：

```text
<project>/<solution>/syn/report/<top>_csynth.rpt
<project>/<solution>/syn/report/<top>_csynth.xml
```

本目录归档了报告，没有包含完整Vitis工程、Solution指令或可一键复现的构建脚本。需要在本地重新配置并生成工程。

## 8. 测试入口

所有测试文件位于 `Edition1/test/`。选择对应顶层，每次只添加表中的一个测试文件。

| Test Bench | Top Function | 检查内容 |
|---|---|---|
| `tb_plif.cpp` | `plif_step` | 状态更新、达到阈值、硬重置 |
| `tb_attention.cpp` | `attention_unit` | 单点XOR计数、PLIF、V门控 |
| `tb_attention_point.cpp` | `attention_point` | 参考编号到K的读取 |
| `tb_attention_channel.cpp` | `attention_channel` | 单通道256点 |
| `tb_attention_frame.cpp` | `attention_frame` | 96通道和跨时间步状态 |
| `tb_mean_distance.cpp` | `mean_squared_distance` | 平均距离、累加器范围 |
| `tb_density.cpp` | `estimate_density` | 定点倒数、零距离 |
| `tb_raw_density.cpp` | `compute_raw_density` | 全部点原始密度 |
| `tb_mean_density.cpp` | `mean_density` | 全样本均值、累加器范围 |
| `tb_density_variance_std.cpp` | `density_variance_std` | 样本方差、开方、epsilon保护 |
| `tb_normalize_clip.cpp` | `normalize_clip_density` | 正负值、截断、精度和零分母保护 |
| `tb_select_density_refs.cpp` | `select_density_refs` | 单点Top-4、并列与去重 |
| `tb_select_all_density_refs.cpp` | `select_all_density_refs` | 256点选择、行间独立性 |
| `tb_density_selector.cpp` | `density_selector` | 距离到参考编号的完整选择链 |
| `tb_sada_attention_kernel.cpp` | `sada_attention_kernel` | 完整连接、两个连续时间步 |

完整测试的预期输出：

```text
Step 0 passed.
Step 1 passed.
Full attention chain passed.
```

### 测试覆盖范围

- 当前输入主要是人工构造的距离、候选编号和脉冲，未证明它们来自同一份真实几何点云。
- `tb_density_selector.cpp` 用浮点公式生成参考选点，但使用定点化后的epsilon，并采用当前硬件的并列规则。
- `tb_sada_attention_kernel.cpp` 复用 `density_selector()` 生成参考编号，再独立计算XOR、PLIF与门控；它是连接测试，不是对整个选择器的第二份独立实现。
- 尚需真实16NN、训练后Q/K/V和PLIF参数，对照原始PyTorch结果；量化导致的近似并列和阈值翻转需要单独统计。

## 9. Edition1 综合基线

以下数据直接摘自已归档报告，生成时间 **2026-11-14 13:24:44**。不代表上板实测结果。

| 指标 | Edition1 | 器件可用 |
|---|---:|---:|
| Target period | 20.00 ns | — |
| Estimated period | 14.414 ns | — |
| Clock uncertainty | 5.40 ns | — |
| Latency | 108,765 cycles，约2.175 ms | — |
| Top-level interval | 108,766 cycles | — |
| LUT | **21,317** | **17,600，约121%** |
| FF | 17,260 | 35,200 |
| DSP | 23 | 80 |
| BRAM_18K | 12 | 120 |
| URAM | 0 | 0 |

**当前LUT估计超出7010容量，不能作为已可部署版本。** 需要降低资源并留出接口、控制和存储系统的余量，再进行完整Vivado实现。

| 主要部分 | LUT | DSP | 延迟 |
|---|---:|---:|---:|
| `density_selector` 实例 | 20,294 | 20 | 10,454 cycles，约0.209 ms |
| `attention_unit` 实例 | 490 | 3 | 3 cycles，调用间隔4 cycles |
| 顶层脉冲计算循环，24,576次迭代 | 包含于实例及顶层逻辑 | — | 98,308 cycles，约1.966 ms，II=4 |

资源重点在密度选择器；当前延迟重点在参考K读取与脉冲计算循环。子模块用量不包含所有顶层控制开销，不能只将计算单元资源相加作为完整资源。

报告中的 `BRAM_18K` 以18 Kbit块计数，不能与36 Kbit块数直接比较。外部输入、输出和膜电位数组当前采用 `ap_memory` 接口，它们的外部存储及控制器并未全部计入该12块BRAM。

当前尚未连接AXI/DDR接口。总延迟不包含CPU找16NN、CPU/FPGA数据搬运或完整模型其他层；HLS时序估计也不能替代Vivado布局布线后的时序与实测频率。

## 10. 四组实验与后续工作

| 配置 | 选择策略 | 比较运算 | 当前情况 |
|---|---|---|---|
| A | 最近8点 | 有符号减法 | 待实现 |
| B | 最近8点 | XOR | 已有最近8点开发路径，待形成同接口基线 |
| C | 全部16个候选 | XOR | 待实现 |
| D | 最近4点＋密度差异4点 | XOR | Edition1完整顶层 |

不要仅修改 `N_REFS=16` 就认为完成配置C：当前密度选择器要求 `N_REFS=N_NEAR+N_DENSITY`，部分测试也固定为8参考。需要把选点策略与公共计算部分明确分开。

接下来按以下顺序推进：

1. 保留Edition1源文件、测试与报告作为基线，后续版本单独归档。
2. 补齐四种配置，统一候选域、排除自身规则、数据、接口、位宽和时钟约束。
3. 使用真实软件导出数据与训练参数，完成逐层精度对照。
4. 在不省略密度归一化和截断的前提下，统一优化位宽、运算复用、流水和缓存。
5. 接入PS/PL数据接口，完成Vivado综合、布局布线和板端验证。
6. 分别报告工具估计与上板测量的资源、频率、延迟、吞吐率和能耗，并计入CPU候选准备与传输开销。

有符号减法的差异值可为负，不能直接复用无符号XOR计数类型。也不能通过不必要的宽整数减法放大XOR的优势；是否节省资源、密度选择是否值得，最终由公平条件下的综合和测量结果判断。
