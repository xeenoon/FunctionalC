fn identityFn(x) { return identity(x); }
fn add(accum, next) { return plus(accum, next); }

of(1, 1, 2, 2, 3, 3, 4).pipe(
    distinct(identityFn),
    reduce(add, 0)
).subscribe(assign(result_sum));
