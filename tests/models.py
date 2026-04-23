from __future__ import annotations

from dataclasses import dataclass
import json
import re


@dataclass
class BenchmarkResult:
    result: int
    average_ms: float
    runs: int
    n: int

    @classmethod
    def parse_json(cls, output: str) -> BenchmarkResult:
        payload = json.loads(output)
        payload['result'] = int(payload['result'])
        return cls(**payload)

    @classmethod
    def parse_codegen_output(cls, output: str, n: int) -> BenchmarkResult:
        result_match = re.search(r'result\s*:\s*(-?\d+)', output)
        average_match = re.search(
            r'average:\s*([0-9]+(?:\.[0-9]+)?)\s*ms\s*\((\d+)\s*runs\)', output
        )
        if result_match is None or average_match is None:
            raise ValueError(f'Unable to parse generated benchmark output: {output}')

        return cls(
            result=int(result_match.group(1)),
            average_ms=float(average_match.group(1)),
            runs=int(average_match.group(2)),
            n=n,
        )
