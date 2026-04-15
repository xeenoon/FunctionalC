fn ltTen(x) { return lt(x, 10); }

range(1, N).pipe(
    takeWhile(ltTen)
).subscribe(assign(result_sum));
