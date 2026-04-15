fn isEven(x) { return eq(mod(x, 2), 0); }

range(1, 10000).pipe(
    filter(isEven)
).subscribe(assign(result_sum));
