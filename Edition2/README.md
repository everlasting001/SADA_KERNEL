# Edition2 — Four HLS Experiments

当前规模：**512点、96通道、一个物体、一次调用一个时间步**。完整接口、定点格式、报告解释及多时间步约定见 [工程README](../README.md)。

## 配置

设计文件和测试文件使用相同的编译选项 `-DSADA_EXPERIMENT=N`：

| N | 参考策略 | 比较方式 |
|---|---|---|
| 0 | 最近8点 | 有符号减法 |
| 1 | 最近8点 | XOR |
| 2 | 全部16个公共候选 | XOR |
| 3 | 最近4点＋密度差异4点 | XOR |

不指定宏时默认3，每组需分别重新编译和综合。`REFERENCE_COUNT` 自动取8或16，密度分支固定4+4。减法使用2位操作数，四组统一用6位有符号 `attention_sum_t` 保存累加结果。

## 文件与运行

- [src/sada_kernel.h](src/sada_kernel.h)：配置、类型、接口。
- [src/sada_kernel.cpp](src/sada_kernel.cpp)：参考选择、比较累加、PLIF和门控。
- [test/tb_sada_experiments.cpp](test/tb_sada_experiments.cpp)：统一测试。

```text
Top: sada_attention_kernel
Part: xc7z010clg400-1
Clock: 20 ns
Design: src/sada_kernel.cpp
Test Bench: test/tb_sada_experiments.cpp
C flags for BOTH files:
    -std=c++11 -I<absolute_src_path> -DSADA_EXPERIMENT=N
```

每次只加入这一个带main的测试。运行 C Simulation → C Synthesis → C/RTL Co-Simulation。当前未归档统一构建脚本及完整板级工程。

## 2026-11-14归档结果

四组C仿真0错误、Verilog协同仿真Pass。以下为HLS综合估计，不是上板实测：

| N | LUT | FF | DSP | BRAM_18K | 延迟/周期 | 延迟/ms |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 1,128 | 384 | 3 | 2 | 200,712 | 4.01424 |
| 1 | 1,056 | 384 | 3 | 2 | 200,712 | 4.01424 |
| 2 | 1,402 | 399 | 3 | 5 | 401,416 | 8.02832 |
| 3 | 21,354 | 17,302 | 27 | 12 | 217,310 | 4.34620 |

报告路径：

```text
report/experiment_N/csim/sada_attention_kernel_csim.log
report/experiment_N/syn/sada_attention_kernel_csynth.rpt
report/experiment_N/syn/sada_attention_kernel_csynth.xml
report/experiment_N/sim/sada_attention_kernel_cosim.rpt
```

配置3超过7010的17,600 LUT容量。当前为 `ap_memory` 数组接口，尚未连接PS/DDR；输入和状态的外部存储、PS控制及传输时间不包含在这些数字中。

## 多时间步：初步采用PS逐步调用

PS准备本步Q/K/V并启动一次PL，等待计算及状态写回完成，再启动下一步。膜电位在序列开始初始化一次，时间步之间保留；不用每步重新配置位流。

候选编号和距离在静态样本内共用，可留在DDR；但当前顶层每步仍重算参考点选择。跨步缓存参考点尚未实现，不能把相关成本当成已被消除。

此方案便于调试每一步。板级控制与AXI/DDR封装仍待实现，后续若启动/传输开销显著，再评估PL内部连续执行T步。正式比较需要同时记录PL计算时间和PS可见的序列总时间。
