fn square(x) { return mul(x, x); }
fn isEven(x) { return eq(mod(x, 2), 0); }
fn add(accum, next) { return plus(accum, next); }

range(1, N).pipe(
    map(square),
    filter(isEven),
    reduce(add, 0)
).subscribe(assign(result_sum));
