pipeline zip_pointer_state_smoke
source zip synthetic_records 5 synthetic_records 5
pairMap pair_boxed
map pair_sum_boxed
scanfrom sum_boxed NULL
map unbox_value
reduce sum 0
