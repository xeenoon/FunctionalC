from dataclasses import dataclass
import json


@dataclass
class BenchmarkResult:
    result: int
    average_ms: float
    runs: int
    n: int

    @classmethod
    def parse(cls, output: str) -> BenchmarkResult:
        return cls(**json.loads(output))
