range(1, 10000).pipe(
    skip(9990)
).subscribe(assign(result_sum));
