fn ltTen(x) { return lt(x, 10); }

range(1, 10000).pipe(
    skipWhile(ltTen)
).subscribe(assign(result_sum));
