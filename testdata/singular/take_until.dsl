range(1, N).pipe(
    takeUntil(10)
).subscribe(assign(result_sum));
