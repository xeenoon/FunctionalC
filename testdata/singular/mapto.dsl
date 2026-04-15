range(1, 10000).pipe(
    mapTo(7)
).subscribe(assign(result_sum));
