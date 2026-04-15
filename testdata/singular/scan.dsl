fn add(accum, next) { return plus(accum, next); }

range(1, 1000).pipe(
    scan(add)
).subscribe(assign(result_sum));
