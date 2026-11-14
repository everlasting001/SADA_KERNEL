open_project -reset project
set_top sada_attention_kernel
add_files src/sada_kernel.cpp -cflags {-std=c++11 -IC:/fpga/sada_edition3_runs/18_reference32_exp2/src -DSADA_EXPERIMENT=2}
add_files -tb test/tb_sada_experiments.cpp -cflags {-std=c++11 -IC:/fpga/sada_edition3_runs/18_reference32_exp2/src -DSADA_EXPERIMENT=2}
open_solution solution1 -flow_target vivado
set_part {xc7z010clg400-1}
create_clock -period 20 -name default
csim_design
csynth_design
cosim_design -rtl verilog
export_design -flow impl -rtl verilog -format ip_catalog
exit
