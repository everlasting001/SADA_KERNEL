# Edition3 — implementation optimization

本版对512点、96通道的四种实验进行实际HLS优化。默认采用 **`13_insert_bram`：优先节省LUT，同时缩短计算时间**。算法定义、定点位宽、PLIF更新公式、输入输出及PS逐步调用约定保持一致。

- [优化报告](OPTIMIZATION_REPORT.md)：各轮尝试、四配置结果、布局布线时序与功耗估计。
- [源文件](src/sada_kernel.cpp)、[类型与配置](src/sada_kernel.h)、[统一测试](test/tb_sada_experiments.cpp)。
- [复现步骤与报告目录](optimization/README.md)。
- [原始Edition2说明](../Edition2/README.md)：完整接口、四种实验定义和数值边界。

## What changed

1. **复用密度计算电路。** 原始密度和逐点选择的外层循环关闭流水线；归一化也关闭这层流水线。减少自动展开和大除法器流水线带来的资源开销。
2. **稳定Top-4。** 扫描剩余12个候选，维护4个最佳项。按密度差降序、候选位置升序比较，保持原版同分时的选择顺序。
3. **K缓存。** 每个通道先顺序读取512个K，复制到双端口BRAM。8个参考使用4份副本，16个参考使用8份副本。增加BRAM以提供并行随机读端口。
4. **参考编号按参考槽分bank。** 配合K缓存，使逐点计算循环达到II=1。仍逐通道执行，每个通道包含缓存加载和点计算两个阶段。

`reduce_reference_spikes()` 与 `update_plif()` 的计算表达式没有修改。四种配置统一使用相同缓存和调度原则，不为XOR单独提供减法没有的优化。

## Build and inspect

在Vitis HLS中：

```text
Top function: sada_attention_kernel
Design source: src/sada_kernel.cpp
Test bench: test/tb_sada_experiments.cpp
Part: xc7z010clg400-1
Clock: 20 ns
C flags for BOTH design and test:
    -std=c++11 -I<absolute_Edition3_src_path> -DSADA_EXPERIMENT=N
```

`N=0/1/2/3` 分别是减法最近8、XOR最近8、XOR全部16、XOR密度选择8。四组分别构建；测试台仅保留一个main，勿定义NDEBUG。

自动构建示例，在Edition3目录执行：

```bash
python optimization/run_trials.py 13_insert_bram --experiment 3 --stage impl
```

脚本以优化前快照生成对应试验源码。默认实现与本目录 `src` 相同，源文件哈希记录在试验 `manifest.json` 中。需要测试新的源码修改时，应新增试验profile或另建HLS工程，避免生成器覆盖手改文件。

`report/` 已由用户更新为默认优化版四配置的HLS综合和C/RTL仿真报告，顶层资源、周期及仿真结果已核对一致。`optimization/trials/13_insert_bram_exp0` 至 `exp3` 保留完整构建快照及Vivado实现报告。`report/experiment_3/syn/` 中残留的10个旧分模块报告已清理，四组综合报告的文件名集合均与默认试验归档一致；路径及哈希见[清理记录](optimization/cleanup_legacy_reports.json)。

## Execution boundary

一次顶层调用仍处理一个样本的一个时间步。PS跨步保留膜电位；新样本开始才重置。本版每次调用重新计算密度选点，尚未实现跨时间步选点缓存。

当前仍是数组端口；Vivado结果属于独立IP的out-of-context实现。未完成PS、AXI、DDR接口和板级部署，外部膜电位等存储不完整计入IP片内资源。功耗属于默认活动率估计，不是整板实测。
