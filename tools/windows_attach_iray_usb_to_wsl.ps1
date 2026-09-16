param(
    [string]$BusId,
    [string]$Distribution,
    [switch]$Detach
)

$ErrorActionPreference = 'Stop'
$TargetVidPid = '3474:43c1'

function Fail([string]$Message) {
    Write-Error $Message
    exit 1
}

function Invoke-Usbipd([string[]]$Arguments) {
    $savedErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & usbipd @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedErrorAction
    }
    if ($exitCode -ne 0) {
        Write-Host ($output -join [Environment]::NewLine)
        Fail "usbipd $($Arguments -join ' ') failed with exit code $exitCode"
    }
    return $output
}

$usbipd = Get-Command usbipd.exe -ErrorAction SilentlyContinue
if ($null -eq $usbipd) {
    Fail '未找到 usbipd-win。请先在 Windows 安装 usbipd-win，然后重新运行本脚本；脚本不会自动安装。'
}

if ($Detach) {
    if ([string]::IsNullOrWhiteSpace($BusId)) {
        Fail 'Detach 模式必须提供 -BusId，例如 -Detach -BusId 4-2'
    }
    Invoke-Usbipd @('detach', '--busid', $BusId) | ForEach-Object { Write-Host $_ }
    Write-Host "已请求解除 WSL 挂载：$BusId"
    exit 0
}

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Fail '绑定 USB 设备需要管理员 PowerShell。请右键 PowerShell 选择“以管理员身份运行”后重新执行。'
}

$list = Invoke-Usbipd @('list')
$targetLine = $list | Where-Object { $_ -match $TargetVidPid } | Select-Object -First 1
if ($null -eq $targetLine) {
    Write-Host ($list -join [Environment]::NewLine)
    Fail "没有发现原厂 iRay USB 设备 $TargetVidPid。请确认原厂 USB 板已连接；不要把 CH32 NCM 设备挂载到 WSL。"
}

if ([string]::IsNullOrWhiteSpace($BusId)) {
    $busMatch = [regex]::Match([string]$targetLine, '(?<bus>\d+-\d+)\s+' + [regex]::Escape($TargetVidPid))
    if (-not $busMatch.Success) {
        Write-Host ($list -join [Environment]::NewLine)
        Fail "找到 $TargetVidPid，但无法从 usbipd 输出解析 BUSID。请使用 -BusId 手工指定该行的 BUSID。"
    }
    $BusId = $busMatch.Groups['bus'].Value
}

$specifiedLine = $list |
    Where-Object { $_ -match ([regex]::Escape($BusId) + '\s+' + [regex]::Escape($TargetVidPid)) } |
    Select-Object -First 1
if ($null -eq $specifiedLine) {
    Write-Host ($list -join [Environment]::NewLine)
    Fail "指定 BUSID $BusId 不是目标设备 $TargetVidPid，拒绝继续。"
}

Write-Host "目标设备：$TargetVidPid，BUSID：$BusId"
Write-Host '执行 bind；若设备已绑定，usbipd 的提示可忽略。'
$savedErrorAction = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $bindOutput = & usbipd bind --busid $BusId 2>&1
    $bindExitCode = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $savedErrorAction
}
if ($bindExitCode -ne 0 -and ($bindOutput -join ' ') -notmatch 'already|已绑定|shared|共享') {
    Write-Host ($bindOutput -join [Environment]::NewLine)
    Fail "usbipd bind failed with exit code $bindExitCode"
}

$attachArgs = @('attach', '--wsl', '--busid', $BusId)
if (-not [string]::IsNullOrWhiteSpace($Distribution)) {
    $attachArgs += @('--distribution', $Distribution)
}
$savedErrorAction = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $attachOutput = & usbipd @attachArgs 2>&1
    $attachExitCode = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $savedErrorAction
}
if ($attachExitCode -ne 0) {
    if (($attachOutput -join ' ') -match 'already attached|already connected|已附加|已连接') {
        Write-Host '设备已经处于 Attached 状态，继续执行验证。'
        $attachExitCode = 0
    }
}
if ($attachExitCode -ne 0) {
    # Compatibility path for older usbipd-win releases.
    $legacyAttachArgs = @('wsl', 'attach', '--busid', $BusId)
    $newAttachOutput = $attachOutput
    $savedErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $attachOutput = & usbipd @legacyAttachArgs 2>&1
        $legacyAttachExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedErrorAction
    }
    if ($legacyAttachExitCode -ne 0) {
        Write-Host '新版 usbipd attach 输出：'
        Write-Host ($newAttachOutput -join [Environment]::NewLine)
        Write-Host '旧版回退输出：'
        Write-Host ($attachOutput -join [Environment]::NewLine)
        Fail "usbipd attach failed with exit code $legacyAttachExitCode; 请保留上面两段输出"
    }
}
$attachOutput | ForEach-Object { Write-Host $_ }

Write-Host 'Windows 侧挂载状态：'
Invoke-Usbipd @('list') | ForEach-Object { Write-Host $_ }

$wslArgs = @()
if (-not [string]::IsNullOrWhiteSpace($Distribution)) {
    $wslArgs += @('-d', $Distribution)
}
$wslArgs += @('--', 'bash', '-lc', "if command -v lsusb >/dev/null 2>&1; then lsusb -d $TargetVidPid || true; else echo 'WSL 内未安装 lsusb；请以 usbipd list 的 Attached 状态为准。'; fi")
$savedErrorAction = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $verify = & wsl.exe @wslArgs 2>&1
    $verifyExitCode = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $savedErrorAction
}
Write-Host 'WSL 侧验证：'
Write-Host ($verify -join [Environment]::NewLine)
if ($verifyExitCode -ne 0) {
    Fail 'WSL 验证命令失败；先检查 Distribution 名称和 WSL 是否正常运行。'
}

Write-Host ''
Write-Host '完成：原厂 iRay USB 设备已尝试挂载到 WSL。接下来在 WSL 中运行：'
Write-Host '  cd /home/slam/Sipeed/T384'
Write-Host '  bash tools/read_wn2256_calibration.sh /home/slam/Sipeed/T384/out/radiometry/wn2256_probe'
