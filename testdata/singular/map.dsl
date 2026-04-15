fn square(x) { return mul(x, x); }

range(1, 10000).pipe(
    map(square)
).subscribe(assign(result_sum));
