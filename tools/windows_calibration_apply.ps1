param(
    [string]$ReportPath = 'out\radiometry\blackbody\20260922T101901.365405Z\report.json',
    [switch]$VerifyAfterPowerCycle
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BaseUrl = 'http://192.168.17.1'
$Report = if ([System.IO.Path]::IsPathRooted($ReportPath)) {
    $ReportPath
} else {
    Join-Path $ProjectRoot $ReportPath
}
$CalibrateScript = Join-Path $ProjectRoot 'tools\calibrate_t384_blackbody.py'

if (-not (Test-Path -LiteralPath $Report -PathType Leaf)) {
    throw "找不到已完成的黑体报告：$Report"
}
if (-not (Test-Path -LiteralPath $CalibrateScript -PathType Leaf)) {
    throw "找不到标定脚本：$CalibrateScript"
}

# Read-only preflight. Do not apply to a 640 image-only build or an active stream.
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
try {
    $statusPreflight = Invoke-WebRequest -UseBasicParsing -Proxy $null -TimeoutSec 5 `
        -Uri ($BaseUrl + '/api/v1/calibration/v1/status')
} catch {
    throw '当前板上没有新的标定持久化状态接口（HTTP 404）；请先下载匹配的 V3F+V5F Merge.bin，避免把提交误判为断电验证成功。'
}
if ($statusPreflight.StatusCode -ne 200) {
    throw "标定持久化状态接口返回 HTTP $($statusPreflight.StatusCode)。"
}

Write-Host "即将把现有 0/50°C 实验报告写入 MCU 隔离经验标定槽："
Write-Host $Report
Write-Host '不会写入 MINI2 原厂标定区，也不会启用 OEM KT/BT/NUC-T 链。'
$confirmation = Read-Host '输入 APPLY 继续'
if ($confirmation -cne 'APPLY') {
    Write-Host '已取消，未写入设备。'
    exit 2
}

& py -3 $CalibrateScript `
    --url ($BaseUrl + '/raw16.stream') `
    --apply-from $Report
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    throw "标定脚本未完成启动持久化校验，退出码：$exitCode。若错误为 status HTTP 404，PUT/commit/立即回读可能已经成功；请先导出当前 manifest，不要重复提交，再更新匹配的 V3F+V5F 镜像。"
}
$status = Invoke-WebRequest -UseBasicParsing -Proxy $null -TimeoutSec 5 `
    -Uri ($BaseUrl + '/api/v1/calibration/v1/status')
if ($status.StatusCode -ne 200) {
    throw "设备没有提供持久化状态接口；请确认已烧录包含 Flash 保存修复的 384 双核镜像。"
}
Write-Host '应用后启动扫描状态：'
Write-Host $status.Content
$beforeStatus = $status.Content | ConvertFrom-Json

if ($VerifyAfterPowerCycle) {
    Read-Host '现在完全断电并重新上电；确认诊断页可达后按回车继续'
    $after = Invoke-WebRequest -UseBasicParsing -Proxy $null -TimeoutSec 5 `
        -Uri ($BaseUrl + '/api/v1/calibration/v1/status')
    if ($after.StatusCode -ne 200) {
        throw '断电重启后无法读取持久化状态接口。'
    }
    Write-Host '断电重启后启动扫描状态：'
    Write-Host $after.Content
    $afterStatus = $after.Content | ConvertFrom-Json
    if (-not $afterStatus.has_valid_slot) {
        throw '断电重启后没有有效标定槽；请保留上述状态和串口/diag证据。'
    }
    if ($afterStatus.generation -ne $beforeStatus.generation) {
        throw "断电重启后 generation 变化：写入前 $($beforeStatus.generation)，重启后 $($afterStatus.generation)。"
    }
}
Write-Host '实验标定包应用完成；请刷新页面并验证当前 profile 的显示。'
