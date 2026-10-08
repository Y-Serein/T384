param(
    [double]$ValidationC,
    [double]$MaxErrorC,
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BaseUrl = 'http://192.168.17.1'
$CalibrateScript = Join-Path $ProjectRoot 'tools\calibrate_t384_blackbody.py'
$Arguments = @(
    '-3', $CalibrateScript,
    '--url', ($BaseUrl + '/raw16.stream'),
    '--low-c', '0',
    '--high-c', '50',
    '--distance-m', '0.01',
    '--emissivity', '0.98',
    '--frames', '30',
    '--gain', 'current'
)

if ($PSBoundParameters.ContainsKey('ValidationC')) {
    $Arguments += @('--validation-c', $ValidationC.ToString([Globalization.CultureInfo]::InvariantCulture))
    if (-not $PSBoundParameters.ContainsKey('MaxErrorC')) {
        throw '-ValidationC 必须同时提供 -MaxErrorC，才允许验证/应用。'
    }
    $Arguments += @('--max-error-c', $MaxErrorC.ToString([Globalization.CultureInfo]::InvariantCulture))
} elseif ($PSBoundParameters.ContainsKey('MaxErrorC')) {
    throw '-MaxErrorC 只能和 -ValidationC 一起使用。'
}

# Read-only preflight, also prevents applying a 384 model to the 640 Picture build.
$response = Invoke-WebRequest -UseBasicParsing -Proxy $null -TimeoutSec 5 `
    -Uri ($BaseUrl + '/diag')
$diag = @{}
foreach ($line in ($response.Content -split "`r?`n")) {
    if ($line -match '^([^=]+)=(.*)$') {
        $diag[$Matches[1]] = $Matches[2]
    }
}
if ($diag['stream.active'] -ne '0') {
    throw '当前仍有活动 RAW16 流；请先关闭成像页和抓流程序。'
}
if ($diag['source.pixel_format'] -ne '2' -or
    $diag['dvp.expected_width'] -ne '384' -or
    $diag['dvp.expected_height'] -ne '288') {
    throw '当前固件不是 384x288 Y16/TPD；请先烧录 384 profile。'
}

if ($Apply) {
    Write-Host '完成采集后将把本次实验模型写入 T384 隔离经验槽。'
    Write-Host '不会写入 MINI2 原厂标定区，也不会启用 OEM KT/BT/NUC-T 链。'
    $confirmation = Read-Host '输入 APPLY 继续'
    if ($confirmation -cne 'APPLY') {
        Write-Host '已取消，未采集/未写入设备。'
        exit 2
    }
    $backupName = 'pre-apply-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.packet'
    $backup = Join-Path $ProjectRoot ('out\radiometry\blackbody\' + $backupName)
    try {
        $manifestResponse = Invoke-WebRequest -UseBasicParsing -Proxy $null -TimeoutSec 5 `
            -Uri ($BaseUrl + '/api/v1/calibration/v1/manifest')
        Write-Host "先备份当前经验槽：$backup"
        & py -3 (Join-Path $ProjectRoot 'tools\calibration_storage_client.py') `
            --url $BaseUrl --export $backup
        if ($LASTEXITCODE -ne 0) {
            throw '旧经验槽导出失败'
        }
    } catch {
        $statusCode = $null
        if ($_.Exception.Response -and $_.Exception.Response.StatusCode) {
            $statusCode = [int]$_.Exception.Response.StatusCode
        }
        if ($statusCode -eq 404) {
            Write-Host '当前没有旧经验包，跳过备份。'
        } else {
            throw
        }
    }
    $Arguments += '--apply'
}

Write-Host '这是重新采集 0/50°C（可选独立验证点）的脚本。'
if (-not $Apply) {
    Write-Host '当前模式只生成原始帧、manifest 和实验 report，不写 MCU/MINI2。'
}
& py @Arguments
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    throw "重新标定采集失败，退出码：$exitCode"
}
