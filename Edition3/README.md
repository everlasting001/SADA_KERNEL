# Edition3 — implementation optimization

本版对512点、96通道的四种实验进行实际HLS优化。当前构建配置为 **`18_reference32`**：沿用 `13_insert_bram` 的电路实现，将实验2单独扩大为32个参考点，其余三组仍为8个。实验2输入32NN、累加器为7位有符号数；其他配置仍输入16NN、使用6位累加器。PLIF公式与PS逐步调用约定不变。

新增结果见 [32参考点实验报告](REFERENCE32_REPORT.md)。原 [优化报告](OPTIMIZATION_REPORT.md) 保留为实验2使用16参考点时的历史结果。

另有用户指定的独立消融：[16候选下去掉归一化和截断](ablation_no_normalization16/README.md)，profile为 `19_raw_density16`。该组直接比较原始密度差，配置2用16参考点，其他用8个；其源码和报告独立保存，不覆盖当前32点版。

原始密度消融版另做了 [查表倒数与真实样本验证](lut_reciprocal16/README.md)：精确除法、256项直接表、64项插值和256项插值均完成硬件验证。数据为真实64通道、4步样本；综合仍保持96通道。表值覆盖完整输入范围，结论与误差边界见该报告，未替换主版本。

- [优化报告](OPTIMIZATION_REPORT.md)：各轮尝试、四配置结果、布局布线时序与功耗估计。
- [源文件](src/sada_kernel.cpp)、[类型与配置](src/sada_kernel.h)、[统一测试](test/tb_sada_experiments.cpp)。
- [复现步骤与报告目录](optimization/README.md)。
- [原始Edition2说明](../Edition2/README.md)：完整接口、四种实验定义和数值边界。

## What changed

1. **复用密度计算电路。** 原始密度和逐点选择的外层循环关闭流水线；归一化也关闭这层流水线。减少自动展开和大除法器流水线带来的资源开销。
2. **稳定Top-4。** 扫描剩余12个候选，维护4个最佳项。按密度差降序、候选位置升序比较，保持原版同分时的选择顺序。
3. **K缓存。** 每个通道先顺序读取512个K，复制到双端口BRAM。8个参考使用4份副本，32个参考使用16份副本。增加BRAM以提供并行随机读端口。
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

`N=0/1/2/3` 分别是减法最近8、XOR最近8、XOR全部32、XOR密度选择8。四组分别构建；测试台仅保留一个main，勿定义NDEBUG。

自动构建示例，在Edition3目录执行：

```bash
python optimization/run_trials.py 18_reference32 --experiment 2 --stage impl
```

`18_reference32` 使用冻结的 `optimization/reference32_snapshot/`；历史优化profile继续使用原快照。源文件哈希记录在试验 `manifest.json` 中。不要用旧 `13_insert_bram` profile 重建当前32参考点版本，它会生成旧16参考点配置。手改源码后应新增快照或另建HLS工程。

当前报告入口为 `report/experiment_N/`；本次32参考点构建的完整归档在 `optimization/trials/18_reference32_expN/`。旧16参考点构建与Vivado实现记录保留在 `optimization/trials/13_insert_bram_expN/`。详细发布状态与结果以 [32参考点实验报告](REFERENCE32_REPORT.md) 为准。此前清理过的10个旧分模块文件记录在[清理记录](optimization/cleanup_legacy_reports.json)。

## Execution boundary

一次顶层调用仍处理一个样本的一个时间步。PS跨步保留膜电位；新样本开始才重置。本版每次调用重新计算密度选点，尚未实现跨时间步选点缓存。

当前仍是数组端口；Vivado结果属于独立IP的out-of-context实现。未完成PS、AXI、DDR接口和板级部署，外部膜电位等存储不完整计入IP片内资源。功耗属于默认活动率估计，不是整板实测。
