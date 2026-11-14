# Current reports: 18_reference32

本目录为当前 `Edition3/src` 的四配置结果：512点、96通道、20 ns，配置2使用32参考点，其余使用8个。四组均通过C仿真、Verilog协同仿真及Vivado独立IP布局布线时序检查。

数据分析见 [32参考点实验报告](../REFERENCE32_REPORT.md)。每个 `experiment_N` 中：

- `syn/`：HLS估计资源、周期与分模块报告。
- `csim/`、`sim/`：C仿真与RTL协同仿真。
- `impl/report/`、`impl/routed/`：Vivado实现资源、内部时序与默认活动率功耗估计。
- `impl/export.zip`：对应IP包，不是板级bitstream。
- `manifest.json`、`result.json`：源文件哈希、参数与提取的结果。

完整可重建的源码快照、测试、Tcl及日志在 `../optimization/trials/18_reference32_expN/`。原16参考点时期的用户报告保存在 [report_before_reference32](../optimization/report_before_reference32/)，旧完整工具记录仍在 `13_insert_bram_expN`。

HLS估计与布局布线结果分开引用。独立IP尚未接入PS/AXI/DDR，功耗不是板级实测。
