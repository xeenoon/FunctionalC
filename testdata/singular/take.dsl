range(1, N).pipe(
    take(10)
).subscribe(assign(result_sum));
