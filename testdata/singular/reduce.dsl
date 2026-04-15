fn add(accum, next) { return plus(accum, next); }

range(1, 1000).pipe(
    reduce(add, 0)
).subscribe(assign(result_sum));
