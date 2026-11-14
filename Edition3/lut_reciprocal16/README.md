# Lookup reciprocal: real-sample experiment

本实验只替换原始密度消融版的 `estimate_density()`：用完整输入范围的归一化尾数查表，代替定点可变除法。仍使用16候选、512点，配置3保留最近4＋密度差Top-4，不恢复z-score或截断。

结果与取舍见 [REPORT.md](REPORT.md)。原32参考点主版本和原始密度精确除法版均保留，没有被覆盖。

已完成四种方案的C仿真、真实样本RTL协同仿真及50 MHz布局布线。取256项插值作为后续精度验证起点：[源码](selected/src/sada_kernel.cpp)、[查表常量](selected/src/reciprocal_lut.h)、[测试](selected/test/tb_sada_experiments.cpp)、[报告](selected/report/)、[资源对比CSV](comparison.csv)。它缩短了耗时，但增加LUT、DSP和BRAM，不代表所有指标都优于精确除法。

```text
Mean distance + epsilon
          |
  Leading one / exponent
          |
   Mantissa in [1, 2)
          |
     Table lookup
          |
 Optional linear interpolation
          |
    Exponent scaling -> density
```

表值由数学公式生成，与用户样本无关；没有按该样本的最小/最大距离裁剪有效范围。额外测试覆盖完整32位平均距离输入、幂次边界、零输入和单调性。

## Data and controls

数据来源：`C:\Users\yinyi\Desktop\scan_old86641_stage1_conv_sample0(1)`。NPY文件只读，输入哈希保存在 [data_analysis.json](data_analysis.json)。

- 几何输入为 `[1,512,16]`，候选无重复、排除自身、距离递增。
- Q/K/V为 `[4,1,64,512]` 的二值脉冲。
- HLS继续使用512点、96通道和20 ns，以便与原96通道基线直接比较。真实样本测试将额外32通道补零，误差只统计原64通道；这些耗时不应称为真实64通道模型的实测耗时。
- 目录没有训练后的 `attn_lif` 参数或参考膜电位。本实验使用受控参数 `decay=0.5, threshold=1, initial_state=0` 检查四步内核，不声称已验证训练后模型精度。
- 比较基准为同一原始密度定点除法器，而非原始PyTorch浮点模型或分类输出。

## Rebuild

在Edition3目录中执行：

```bash
# Selected accurate LUT candidate and paired exact baseline.
python optimization/run_trials.py 22_lut_linear256 --experiment 3 --stage impl
python optimization/run_trials.py 24_lut_exact_control --experiment 3 --stage impl
```

其他已运行候选：`20_lut_constant256`、`21_lut_linear64`。`23_lut_linear1024`只做了Python数值筛查和源码生成，没有硬件运行结果。

冻结源文件、表常量与真实数据测试位于 `../optimization/lut_*_snapshot/`，工具记录位于 `../optimization/trials/PROFILE_exp3/`。构建manifest包含所有源文件、表头与测试fixture的哈希，不能只核对主cpp文件。

`analyze_data.py` 不依赖NumPy，可解析本数据中的小端NPY，检查输入并筛查多种表分辨率。`prepare_variants.py` 用分析结果生成冻结试验，遇到已有快照会拒绝覆盖；旧快照不要随意改动。

`test_lut_domain.cpp` 是额外的native/C测试程序，不能与主HLS测试台的main同时链接。它检查20,101个全域输入与单调性。完整内核的C/RTL验证使用各快照的 `test/tb_sada_experiments.cpp`，包含2步人工输入和4步真实样本。
