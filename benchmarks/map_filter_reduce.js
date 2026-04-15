const { range } = require('rxjs');
const { map, filter, reduce } = require('rxjs/operators');

const N = parseInt(process.argv[2]) || 1000000;
const RUNS = parseInt(process.argv[3]) || 5;

let result = 0;
let totalMs = 0;

for (let run = 0; run < RUNS; run++) {
    result = 0;
    const start = performance.now();

    range(1, N)
        .pipe(
            map((x) => x * x),
            filter((x) => x % 2 === 0),
            reduce((acc, x) => acc + x, 0)
        )
        .subscribe((v) => (result = v));

    const end = performance.now();
    totalMs += end - start;
}

console.log(
    JSON.stringify({ result, average_ms: totalMs / RUNS, runs: RUNS, n: N })
);
