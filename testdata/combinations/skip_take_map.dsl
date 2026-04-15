fn square(x) { return mul(x, x); }

range(1, N).pipe(
    skip(100),
    take(100),
    map(square)
).subscribe(assign(result_sum));
