$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$outDir = Join-Path $root "core/out"
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
        core/tools/operator_registry.c `
        core/tools/lowering.c `
        core/tools/planner.c `
        -o out/pipeline_codegen.exe
}

$benchmarks = Get-ChildItem -Path testdata -Recurse -File -Filter *.dsl | Sort-Object FullName

foreach ($bench in $benchmarks) {
    $relative = $bench.FullName.Substring($root.Length + 1).Replace('\', '_').Replace('/', '_')
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($relative)
    $outC = "out/generated_$baseName.c"
    $outExe = "out/generated_$baseName.exe"

    Invoke-Step "Run $($bench.FullName.Substring($root.Length + 1))" {
        .\out\pipeline_codegen.exe `
            --dsl $bench.FullName `
            --output $outC `
            --binary $outExe `
            --define N=1000000 `
            --runs 5 `
            --compile `
            --run
    }
}
