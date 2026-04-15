$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$outDir = Join-Path $root "out"
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

function Invoke-Step {
    param(
        [string]$Label,
        [scriptblock]$Action
    )

    Write-Host ""
    Write-Host "== $Label =="
    & $Action
}

Invoke-Step "Build DSL Compiler" {
    gcc -Wall -Wextra -g -O0 `
        core/tools/pipeline_codegen.c `
        core/tools/dsl_lexer.c `
        core/tools/dsl_parser.c `
        core/tools/dsl_codegen.c `
        -o out/pipeline_codegen.exe
}

$benchmarks = @(
    @{
        Name = "map-filter-reduce"
        Dsl = "testdata/benchmark.dsl"
        OutC = "out/generated_benchmark.c"
        OutExe = "out/generated_benchmark.exe"
    },
    @{
        Name = "map-take-reduce"
        Dsl = "testdata/benchmark_take.dsl"
        OutC = "out/generated_take.c"
        OutExe = "out/generated_take.exe"
    },
    @{
        Name = "filter-take"
        Dsl = "testdata/benchmark_filter.dsl"
        OutC = "out/generated_filter.c"
        OutExe = "out/generated_filter.exe"
    }
)

foreach ($bench in $benchmarks) {
    Invoke-Step "Run $($bench.Name)" {
        .\out\pipeline_codegen.exe `
            --dsl $bench.Dsl `
            --output $bench.OutC `
            --binary $bench.OutExe `
            --define N=1000000 `
            --runs 5 `
            --compile `
            --run
    }
}
