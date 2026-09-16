param(
    [Parameter(Mandatory = $true)][string]$Output,
    [Parameter(Mandatory = $true)][double]$SetpointC,
    [Parameter(Mandatory = $true)][ValidateSet('high','low')][string]$Gain,
    [Parameter(Mandatory = $true)][double]$DistanceM,
    [Parameter(Mandatory = $true)][double]$Emissivity,
    [int]$Frames = 30,
    [string]$Url = 'http://192.168.18.1/raw16.stream',
    [string]$DiagUrl = 'http://192.168.18.1/diag'
)

$ErrorActionPreference = 'Stop'
$script = Join-Path (Split-Path -Parent $PSCommandPath) 'capture_radiometry_calibration.py'
if (-not (Test-Path -LiteralPath $script -PathType Leaf)) {
    throw "missing capture script: $script"
}

$python = Get-Command py.exe -ErrorAction SilentlyContinue
if (-not $python) { $python = Get-Command python.exe -ErrorAction SilentlyContinue }
if (-not $python) {
    throw '未找到 Python。请使用项目已有 Python 环境，不要在脚本中自动安装依赖。'
}

& $python.Source $script `
    --url $Url `
    --diag-url $DiagUrl `
    --output $Output `
    --setpoint-c $SetpointC `
    --gain $Gain `
    --distance-m $DistanceM `
    --emissivity $Emissivity `
    --frames $Frames
if ($LASTEXITCODE -ne 0) {
    throw "blackbody capture failed with exit code $LASTEXITCODE"
}
