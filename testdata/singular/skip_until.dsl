range(1, 10000).pipe(
    skipUntil(10)
).subscribe(assign(result_sum));
