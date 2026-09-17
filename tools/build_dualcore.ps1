param([string]$MrsRoot)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
try {
    if (-not $MrsRoot) {
        $mrs = Get-Process | Where-Object { $_.ProcessName -like 'MounRiver Studio*' } | Select-Object -First 1
        if (-not $mrs -or -not $mrs.Path) { throw 'Open MRS first, or provide -MrsRoot.' }
        $MrsRoot = Split-Path -Parent $mrs.Path
    }
    $gcc = Get-ChildItem -LiteralPath $MrsRoot -Filter riscv-wch-elf-gcc.exe -Recurse | Select-Object -First 1
    $make = Get-ChildItem -LiteralPath $MrsRoot -Filter make.exe -Recurse | Select-Object -First 1
    if (-not $gcc -or -not $make) { throw 'Existing MRS GCC/make was not found; nothing is installed.' }
    $env:Path = $gcc.DirectoryName + ';' + $make.DirectoryName + ';' + $env:Path
    & $gcc.FullName --version
    foreach ($core in @('V3F', 'V5F')) {
        $buildDir = Join-Path $projectRoot ('firmware\' + $core + '\obj')
        if (-not (Test-Path (Join-Path $buildDir 'makefile'))) { throw "Build $core once in MRS to generate its makefile." }
        Push-Location $buildDir
        try {
            # Rebuild both maps for the shared-source freshness gate; no Clean/download.
            & $make.FullName -B -j16 all
            $buildExitCode = $LASTEXITCODE
        } finally { Pop-Location }
        if ($buildExitCode -ne 0) { exit $buildExitCode }
    }
    $python = Get-Command py.exe -ErrorAction SilentlyContinue
    $pythonArgs = @()
    if ($python) { $pythonArgs += '-3' }
    else { $python = Get-Command python.exe -ErrorAction SilentlyContinue }
    if (-not $python) { throw 'Python 3 is required for merge verification; nothing is installed.' }
    foreach ($tool in @('merge_dualcore_hex.py', 'check_dualcore_artifacts.py')) {
        & $python.Source @pythonArgs (Join-Path $PSScriptRoot $tool)
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    Write-Host 'Both cores and Merge.bin verified. No hardware was programmed.'
    exit 0
} catch { Write-Error $_; exit 1 }
