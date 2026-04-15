from dataclasses import dataclass
from enum import StrEnum
import json


class Scenario(StrEnum):
    map_filter_reduce = 'map_filter_reduce'


@dataclass
class BenchmarkResult:
    result: int
    average_ms: float
    runs: int
    n: int

    @classmethod
    def parse(cls, output: str) -> BenchmarkResult:
        return cls(**json.loads(output))
