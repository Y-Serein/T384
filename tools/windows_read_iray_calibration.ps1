param(
    [string]$OutputDirectory = 'C:\Serein_Y\Sipeed\T384\out\radiometry\wn2256_probe_windows',
    [string]$ComPort
)

$ErrorActionPreference = 'Stop'
$TargetVidPid = '3474:43c1'
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$SdkRoot = Join-Path $ProjectRoot 'tools\t384_web\AC020_win&&linux_SDK'
$IncludeDir = Join-Path $SdkRoot 'libir_SDK_release\include'
$LibDir = Join-Path $SdkRoot 'libir_SDK_release\windows\x64\Release\lib'
$DllDir = Join-Path $SdkRoot 'libir_SDK_release\windows\x64\Release\dll'
$PthreadDll = Join-Path $SdkRoot 'libir_sample\thirdparty\pthreads\libs\x64\dll\pthreadVC2.dll'
$Source = Join-Path $ProjectRoot 'tools\t384_web\tools\iray_tpd_calibration_read.cpp'
$BuildDir = Join-Path $env:TEMP 't384_iray_tpd_probe_windows'
$Binary = Join-Path $BuildDir 'iray_tpd_calibration_read.exe'

function Fail([string]$Message) {
    Write-Error $Message
    exit 1
}

function Find-VsDevCmd {
    $vswhere = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($vswhere) {
        $path = & $vswhere.Source -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($path) {
            $candidate = Join-Path $path 'Common7\Tools\VsDevCmd.bat'
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    $candidates = @(
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat'
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

if (-not (Test-Path -LiteralPath $IncludeDir -PathType Container) -or
    -not (Test-Path -LiteralPath $LibDir -PathType Container) -or
    -not (Test-Path -LiteralPath $Source -PathType Leaf)) {
    Fail "仓库内 Windows SDK 或探针源码不完整：$SdkRoot"
}

Write-Host "Windows 原生模式：不使用 usbipd，不使用 WSL。"
Write-Host "探针将通过原厂 SDK 直接打开 VID:PID=$TargetVidPid。"

# Read-only ownership check: an Attached usbipd device is invisible to the
# native Windows UVC backend even if Device Manager still shows "Camera".
$usbipd = Get-Command usbipd.exe -ErrorAction SilentlyContinue
if ($usbipd) {
    $usbipdList = & usbipd.exe list 2>$null
    $attachedLine = $usbipdList | Where-Object {
        $_ -match ([regex]::Escape($TargetVidPid) + '.*Attached')
    } | Select-Object -First 1
    if ($attachedLine) {
        Fail "检测到 $TargetVidPid 仍被 usbipd 挂载到 WSL。Windows 原生 SDK 无法打开它；先执行 usbipd.exe list 找到 BUSID，再执行 usbipd.exe detach --busid <BUSID>，重新插拔设备后再运行本脚本。"
    }
}

$pnp = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -match 'VID_3474&PID_43C1' }
if ($pnp) {
    Write-Host 'Windows PnP 状态：'
    $pnp | Select-Object Status,Class,FriendlyName,InstanceId |
        Format-Table -AutoSize | Out-String | Write-Host
} else {
    Write-Host "Windows PnP 未找到 VID_3474&PID_43C1；SDK 后续必然返回 -619。请检查设备管理器中的驱动/枚举。"
}

$vsDevCmd = Find-VsDevCmd
if (-not $vsDevCmd) {
    Fail '未找到 MSVC C++ 工具链。请安装 Visual Studio Build Tools 的“使用 C++ 的桌面开发”，或继续使用 WSL 版探针。'
}

New-Item -ItemType Directory -Force -Path $BuildDir, $OutputDirectory | Out-Null
$compile = "call `"$vsDevCmd`" -arch=x64 && cl.exe /nologo /std:c++17 /EHsc /MD /W4 /WX /D_WIN32 /I`"$IncludeDir`" `"$Source`" /Fe:`"$Binary`" /link /LIBPATH:`"$LibDir`" libirupgrade.lib libircmd.lib libiruvc.lib libircam.lib libiruart.lib advapi32.lib"
$compileLog = Join-Path $BuildDir 'compile.log'
cmd.exe /d /s /c $compile *> $compileLog
$compileExitCode = $LASTEXITCODE
Get-Content -LiteralPath $compileLog | ForEach-Object { Write-Host $_ }
if ($compileExitCode -ne 0 -or -not (Test-Path -LiteralPath $Binary)) {
    Fail "Windows 原生标定探针编译失败；完整日志：$compileLog"
}

$dllFiles = Get-ChildItem -LiteralPath $DllDir -Filter '*.dll' -File
if (-not $dllFiles) {
    Fail "Windows SDK DLL 目录为空：$DllDir"
}
Copy-Item -LiteralPath $dllFiles.FullName -Destination $BuildDir -Force
if (-not (Test-Path -LiteralPath $PthreadDll -PathType Leaf)) {
    Fail "缺少 libiruvc.dll 运行时依赖：$PthreadDll"
}
Copy-Item -LiteralPath $PthreadDll -Destination $BuildDir -Force
$env:PATH = "$BuildDir;$DllDir;$env:PATH"
$probeArgs = @($OutputDirectory)
if (-not [string]::IsNullOrWhiteSpace($ComPort)) {
    $probeArgs += $ComPort
    Write-Host "使用 UART 控制通道：$ComPort"
}
& $Binary @probeArgs
if ($LASTEXITCODE -ne 0) {
    if ([string]::IsNullOrWhiteSpace($ComPort) -and $LASTEXITCODE -eq 1) {
        Write-Host '提示：SDK 已启动但 UVC 打开失败（常见原因：Camera/OBS/浏览器占用设备，或 Windows UVC 驱动未释放）。请关闭这些程序后拔插原厂 USB 板再重试。'
    } elseif (-not [string]::IsNullOrWhiteSpace($ComPort)) {
        Write-Host "提示：UART $ComPort 打开或命令通道初始化失败；请确认该 COM 口属于原厂板且为 115200 8N1。"
    }
    Fail "Windows 原生标定探针执行失败，退出码 $LASTEXITCODE"
}

Write-Host "标定探针输出：$OutputDirectory"
