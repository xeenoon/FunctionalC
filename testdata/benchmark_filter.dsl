fn isEven(x) { return eq(mod(x, 2), 0); }

range(1, N).pipe(
    filter(isEven),
    take(5)
).subscribe(v => result_sum = v);
