fn square(x) { return mul(x, x); }
fn isEven(x) { return eq(mod(x, 2), 0); }
fn add(accum, x) { return plus(accum, x); }

range(1, N).pipe(
    map(square),
    filter(isEven),
    reduce(add, 0)
).subscribe(v => result_sum = v);
