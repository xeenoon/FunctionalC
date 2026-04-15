fn square(x) { return mul(x, x); }
fn isEven(x) { return eq(mod(x, 2), 0); }
fn lteTenThousand(x) { return lte(x, 10000); }

range(1, N).pipe(
    map(square),
    filter(isEven),
    takeWhile(lteTenThousand)
).subscribe(assign(result_sum));
