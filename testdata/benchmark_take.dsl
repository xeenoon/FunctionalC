fn square(x) { return mul(x, x); }
fn add(accum, x) { return plus(accum, x); }

range(1, N).pipe(
    map(square),
    take(10),
    reduce(add, 0)
).subscribe(v => result_sum = v);
