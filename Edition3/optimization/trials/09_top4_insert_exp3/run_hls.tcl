open_project project
set_top sada_attention_kernel
add_files src/sada_kernel.cpp -cflags {-std=c++11 -IC:/fpga/sada_edition3_runs/09_top4_insert_exp3/src -DSADA_EXPERIMENT=3}
add_files -tb test/tb_sada_experiments.cpp -cflags {-std=c++11 -IC:/fpga/sada_edition3_runs/09_top4_insert_exp3/src -DSADA_EXPERIMENT=3}
open_solution solution1 -flow_target vivado
set_part {xc7z010clg400-1}
create_clock -period 20 -name default
csim_design
csynth_design
exit
