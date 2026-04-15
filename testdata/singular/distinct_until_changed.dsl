fn identityFn(x) { return identity(x); }

of(1, 1, 2, 2, 3, 3, 4).pipe(
    distinctUntilChanged(identityFn)
).subscribe(assign(result_sum));
