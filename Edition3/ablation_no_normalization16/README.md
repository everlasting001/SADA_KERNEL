# Raw-density selection, 16 candidates

本目录是独立消融实验，基于历史 `13_insert_bram` 的16候选版本。它没有覆盖当前Edition3的32参考点源码或报告。

用户指定：**归一化和截断一起删除，直接比较原始密度差。** 保留平均平方距离、倒数密度估计、最近4＋密度差Top-4、XOR/有符号减法、PLIF与V门控。

```text
16NN distances -> mean distance -> raw density = 1/(mean + epsilon)
                                         |
                      nearest4 + Top4 absolute raw-density difference
                                         |
                            gather K -> compare/sum -> PLIF -> gate V
```

不再计算全样本密度均值、方差、标准差、z-score和[-3,3]截断。它是算法消融，不声称与原带截断版本的选点和模型精度相同。

| 配置 | 比较方式 | 参考数量 |
|---|---|---:|
| 0 | 有符号减法，最近点 | 8 |
| 1 | XOR，最近点 | 8 |
| 2 | XOR，全部候选 | **16** |
| 3 | XOR，最近4＋原始密度差Top-4 | 8 |

原始密度保留 `ap_ufixed<48,24>`，选点差值使用 `ap_fixed<49,25>`、绝对值分数使用 `ap_ufixed<48,24>`，防止直接套用原归一化后的小范围类型而饱和。Q/K/V、比较累加、PLIF位宽、缓存和调度原则与原16点版一致。

结果及资源对比见 [REPORT.md](REPORT.md)，数据表见 [comparison.csv](comparison.csv)。四组均通过C仿真、Verilog协同仿真与50 MHz独立IP布局布线时序检查。当前目录下 `report/experiment_N/` 是这组消融的报告；完整日志、源码快照、参数及IP包在 `../optimization/trials/19_raw_density16_expN/`。

在Edition3目录重建：

```bash
python optimization/run_trials.py 19_raw_density16 --experiment 3 --stage impl
```

手动Vitis HLS设置：顶层 `sada_attention_kernel`，设计文件 `src/sada_kernel.cpp`，测试台 `test/tb_sada_experiments.cpp`，器件 `xc7z010clg400-1`，20 ns。设计与测试都使用同一 `-DSADA_EXPERIMENT=N` 和本目录的头文件路径。

头文件搜索路径必须指向本消融目录的 `src`，不要混入外层 `Edition3/src` 的32参考点头文件。

`test/tb_raw_top4.cpp` 是额外的C/native独立测试程序，不能与主测试台同时作为两个main链接，也不作为整内核RTL协同仿真的测试台。它用64位整数精确参考检查48位定点原始密度的Top-4排序；测试记录见 `test/raw_top4_result.log`。
