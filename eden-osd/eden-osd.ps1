# =====================================================================
#  Eden OSD —— 类 RTSS 的游戏内监控覆盖层（PowerShell 5 + WinForms）
#  数据源：
#    FPS / 帧时间 / 1% Low  -> PresentMon (ETW, 真实呈现帧)
#    GPU 占用/温度/显存/功耗 -> nvidia-smi --loop
#    CPU 占用/频率/内存      -> Win32 API
#  热键：F10 显示/隐藏   F9 退出
# =====================================================================
param([int]$Interval = 200)

$ErrorActionPreference = 'Stop'

$script:Root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$script:LogPath = Join-Path $script:Root 'eden-osd.log'
$script:PmExe   = Join-Path $script:Root 'PresentMon.exe'
$script:CsvPath = Join-Path $script:Root 'eden-pmon.csv'
$script:GpuPath = Join-Path $script:Root 'eden-gpu.csv'

# Lossless Scaling 以管理员权限运行，PresentMon 采集它需要本进程也已提权
$script:IsElevated = $false
try {
    $script:IsElevated = (New-Object Security.Principal.WindowsPrincipal(
        [Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
} catch { }
$script:LsRunning = $false

function Write-OsdLog([string]$msg) {
    try { Add-Content -LiteralPath $script:LogPath -Value ((Get-Date).ToString('MM-dd HH:mm:ss') + '  ' + $msg) -Encoding UTF8 } catch { }
}

$script:ErrLogged = @()
function Write-OsdErr([string]$where, $err) {
    if ($script:ErrLogged -contains $where) { return }
    $script:ErrLogged += $where
    Write-OsdLog ($where + ' 异常: ' + $err)
}

# ---------- 单实例 ----------
# 窗口探测是主判据：同一会话内窗口句柄全局可见，提权/非提权都能互相看到。
# 命名互斥体只作兜底，因为它受完整性级别影响（管理员实例创建后，
# 普通实例打开会抛 UnauthorizedAccessException，旧代码用 catch {} 吞掉后
# 直接往下跑，于是出现两个面板）。
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class EdenOsdSingle {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowW(string lpClassName, string lpWindowName);
}
"@

$script:AlreadyRunning = ([EdenOsdSingle]::FindWindowW($null, 'Eden OSD') -ne [IntPtr]::Zero)
if (-not $script:AlreadyRunning) {
    $script:mutexCreated = $false
    try {
        $script:mutex = New-Object System.Threading.Mutex($true, 'Global\EdenOsdOverlayV1', [ref]$script:mutexCreated)
        if (-not $script:mutexCreated) { $script:AlreadyRunning = $true }
    } catch {
        # 互斥体打不开（多半是被更高权限实例占用），但窗口没找到时不阻断启动
    }
}
if ($script:AlreadyRunning) { return }

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public class EdenOsdNative
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }

    [StructLayout(LayoutKind.Sequential)]
    public struct MEMORYSTATUSEX
    {
        public uint dwLength; public uint dwMemoryLoad;
        public ulong ullTotalPhys; public ulong ullAvailPhys;
        public ulong ullTotalPageFile; public ulong ullAvailPageFile;
        public ulong ullTotalVirtual; public ulong ullAvailVirtual;
        public ulong ullAvailExtendedVirtual;
    }

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
    [DllImport("user32.dll")]
    public static extern int GetWindowLong(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll")]
    public static extern int SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);
    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int x, int y, int cx, int cy, uint uFlags);
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll")]
    public static extern short GetAsyncKeyState(int vKey);
    [DllImport("user32.dll")]
    public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern bool SetProcessDPIAware();

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetSystemTimes(out ulong idle, out ulong kernel, out ulong user);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX m);

    [StructLayout(LayoutKind.Sequential)]
    private struct PPI
    {
        public uint Number, MaxMhz, CurrentMhz, MhzLimit, MaxIdleState, CurrentIdleState;
    }

    [DllImport("powrprof.dll", SetLastError = true)]
    private static extern uint CallNtPowerInformation(int level, IntPtr inBuf, uint inSize, IntPtr outBuf, uint outSize);

    // 当前 CPU 平均频率 (MHz)
    public static int CurrentMhz()
    {
        int n = Environment.ProcessorCount;
        int unit = Marshal.SizeOf(typeof(PPI));
        IntPtr buf = Marshal.AllocHGlobal(unit * n);
        try
        {
            if (CallNtPowerInformation(11, IntPtr.Zero, 0, buf, (uint)(unit * n)) != 0) return 0;
            long total = 0;
            for (int i = 0; i < n; i++)
            {
                PPI p = (PPI)Marshal.PtrToStructure(new IntPtr(buf.ToInt64() + i * unit), typeof(PPI));
                total += p.CurrentMhz;
            }
            return (int)(total / n);
        }
        catch { return 0; }
        finally { Marshal.FreeHGlobal(buf); }
    }

    // 返回 [idle, kernel, user] 的 100ns 计数
    public static ulong[] CpuTimes()
    {
        ulong idle, kernel, user;
        if (!GetSystemTimes(out idle, out kernel, out user)) return null;
        return new ulong[] { idle, kernel, user };
    }

    public static ulong[] MemBytes()
    {
        MEMORYSTATUSEX m = new MEMORYSTATUSEX();
        m.dwLength = (uint)Marshal.SizeOf(typeof(MEMORYSTATUSEX));
        if (!GlobalMemoryStatusEx(ref m)) return null;
        return new ulong[] { m.ullTotalPhys, m.ullAvailPhys };
    }
}
"@

# ---------------------------------------------------------------------
#  系统固定信息（启动时采集一次）
# ---------------------------------------------------------------------
function Get-CpuModel {
    try {
        $v = Get-ItemProperty 'HKLM:\HARDWARE\DESCRIPTION\System\CentralProcessor\0' -ErrorAction Stop
        return ($v.ProcessorNameString -replace '\s+', ' ').Trim()
    } catch { return 'CPU' }
}
function Get-WinBuild {
    try {
        $v = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction Stop
        return ('Win ' + $v.DisplayVersion + ' ' + $v.CurrentBuildNumber)
    } catch { return 'Windows' }
}
function Get-RefreshRate {
    try {
        $vc = Get-CimInstance Win32_VideoController -ErrorAction Stop |
              Where-Object { $_.CurrentRefreshRate -gt 20 -and $_.Name -match 'NVIDIA|AMD|Intel' } |
              Select-Object -First 1
        if ($vc) { return [int]$vc.CurrentRefreshRate }
    } catch { }
    return 60
}

$script:CpuModel  = Get-CpuModel
$script:WinBuild  = Get-WinBuild
$script:RefreshHz = Get-RefreshRate
$script:Cores     = (Get-CimInstance Win32_Processor -ErrorAction SilentlyContinue | Measure-Object -Property NumberOfCores -Sum).Sum
if (-not $script:Cores) { $script:Cores = [Environment]::ProcessorCount }
$script:Threads   = [Environment]::ProcessorCount

# ---------------------------------------------------------------------
#  PresentMon / nvidia-smi 子进程管理
# ---------------------------------------------------------------------
$script:Pmon  = $null
$script:GpuPs = $null

function Stop-OsdChild {
    if ($script:Pmon)  { try { if (-not $script:Pmon.HasExited)  { $script:Pmon.Kill() }  } catch { } ; $script:Pmon = $null }
    if ($script:GpuPs) { try { if (-not $script:GpuPs.HasExited) { $script:GpuPs.Kill() } } catch { } ; $script:GpuPs = $null }
}

# 上一次实例被强杀时，nvidia-smi / PresentMon 子进程会残留在后台，
# 启动时先把这类孤儿进程清掉，避免越积越多
function Reset-OsdChild {
    try {
        Get-CimInstance Win32_Process -Filter "Name='nvidia-smi.exe'" -ErrorAction SilentlyContinue |
            Where-Object { $_.CommandLine -like '*--loop=1*' } |
            ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
        Get-CimInstance Win32_Process -Filter "Name='PresentMon.exe'" -ErrorAction SilentlyContinue |
            Where-Object { $_.CommandLine -like '*--session_name EdenOsd*' } |
            ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    } catch { }
}

function Start-PresentMon {
    if (-not (Test-Path $script:PmExe)) { Write-OsdLog 'PresentMon.exe 不存在'; return }
    if ($script:Pmon -and -not $script:Pmon.HasExited) { return }
    try {
        if ($script:Pmon) { $script:Pmon.Dispose() }
        $script:Pmon = $null
        # 清理残留 ETW 会话：同一时间只允许一个内核 provider 会话，
        # 否则 PresentMon 虽在运行却抓不到任何帧（CSV 不生成）
        foreach ($sess in @('EdenOsd', 'PresentMon')) {
            try {
                $li = New-Object System.Diagnostics.ProcessStartInfo
                $li.FileName        = (Join-Path $env:SystemRoot 'System32\logman.exe')
                $li.Arguments       = 'stop ' + $sess + ' -ets'
                $li.UseShellExecute = $false
                $li.CreateNoWindow  = $true
                $lp = [System.Diagnostics.Process]::Start($li)
                [void]$lp.WaitForExit(1200)
            } catch { }
        }
        Remove-Item -LiteralPath $script:CsvPath -ErrorAction SilentlyContinue
        # --output_file 写出的 CSV 被 PresentMon 独占锁定，本进程读不到；
        # 改用 --output_stdout 由 PowerShell 重定向落盘，可并发读取。
        $errPath = Join-Path $script:Root 'eden-pmon.err'
        $script:Pmon = Start-Process -FilePath $script:PmExe -PassThru -WindowStyle Hidden `
            -ArgumentList @(
                # 一个会话同时采集游戏与 Lossless Scaling，按 CSV 的 Application 列分流；
                # LS 以管理员运行，未提权时这一路会抓不到（面板显示 --）
                '--process_name', 'eden-cli.exe',
                '--process_name', 'LosslessScaling.exe',
                '--output_stdout', '--v1_metrics',
                '--session_name', 'EdenOsd', '--stop_existing_session') `
            -RedirectStandardOutput $script:CsvPath `
            -RedirectStandardError $errPath
        $script:csvOffset   = 0
        $script:csvRest     = ''
        $script:csvHeader   = $false
        Write-OsdLog 'PresentMon 已启动'
    } catch {
        Write-OsdLog ('PresentMon 启动失败: ' + $_.Exception.Message)
    }
}

function Start-GpuLoop {
    if ($script:GpuPs -and -not $script:GpuPs.HasExited) { return }
    if (-not (Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue)) {
        $cand = Join-Path $env:SystemRoot 'System32\nvidia-smi.exe'
        if (-not (Test-Path $cand)) { Write-OsdLog '未找到 nvidia-smi，GPU 指标禁用'; return }
    }
    try {
        Remove-Item -LiteralPath $script:GpuPath -ErrorAction SilentlyContinue
        $script:GpuPs = Start-Process -FilePath 'nvidia-smi.exe' -PassThru -WindowStyle Hidden `
            -ArgumentList @(
                '--query-gpu=name,utilization.gpu,temperature.gpu,memory.used,memory.total,power.draw,clocks.gr,driver_version',
                '--format=csv,noheader,nounits', '--loop=1') `
            -RedirectStandardOutput $script:GpuPath
        Write-OsdLog 'nvidia-smi 轮询已启动'
    } catch {
        Write-OsdLog ('nvidia-smi 启动失败: ' + $_.Exception.Message)
    }
}

# ---------------------------------------------------------------------
#  PresentMon CSV 增量读取
# ---------------------------------------------------------------------
$script:csvOffset = 0
$script:csvRest   = ''
$script:csvHeader = $false
$script:Frames    = New-Object 'System.Collections.Generic.List[double]'
$script:FrameT    = New-Object 'System.Collections.Generic.List[double]'
$script:LsFrames  = New-Object 'System.Collections.Generic.List[double]'
$script:LsFrameT  = New-Object 'System.Collections.Generic.List[double]'
$script:LsFps     = 0.0
$script:LsValid   = $false
$script:DropCnt   = 0
$script:PresentMode = ''

function Read-NewFrames {
    if (-not (Test-Path -LiteralPath $script:CsvPath)) { return }
    try {
        $len = (Get-Item -LiteralPath $script:CsvPath).Length
        if ($len -lt $script:csvOffset) {   # 文件被重建
            $script:csvOffset = 0; $script:csvRest = ''; $script:csvHeader = $false
            $script:csvEncReady = $false; $script:csvUtf16 = $false
            $script:Frames.Clear(); $script:FrameT.Clear(); $script:DropCnt = 0
            $script:LsFrames.Clear(); $script:LsFrameT.Clear()
            $script:LsValid = $false; $script:LsFps = 0.0
        }
        if ($len -le $script:csvOffset) { return }

        $fs = [System.IO.File]::Open($script:CsvPath, 'Open', 'Read', 'ReadWrite')
        try {
            # PowerShell 的 stdout 重定向可能写出 UTF-16LE（带 BOM 或不带 BOM），
            # 也可能直接是 UTF-8；首读时按前 4 字节判定编码
            if (-not $script:csvEncReady -and $len -ge 4) {
                $h = New-Object byte[] 4
                [void]$fs.Seek(0, 'Begin')
                [void]$fs.Read($h, 0, 4)
                if ($h[0] -eq 0xFF -and $h[1] -eq 0xFE) {
                    $script:csvUtf16 = $true
                    $script:csvOffset = 2
                } elseif ($h[0] -ne 0 -and $h[1] -eq 0 -and $h[3] -eq 0) {
                    # UTF-16LE 无 BOM（如 "A\0p\0"）
                    $script:csvUtf16 = $true
                    $script:csvOffset = 0
                } else {
                    $script:csvUtf16 = $false
                    $script:csvOffset = 0
                }
                $script:csvEncReady = $true
            }
            if ($len -le $script:csvOffset) { return }

            [void]$fs.Seek($script:csvOffset, 'Begin')
            $want = $len - $script:csvOffset
            if ($script:csvUtf16 -and (($want % 2) -eq 1)) { $want = $want - 1 }
            if ($want -le 0) { return }
            $buf  = New-Object byte[] $want
            $read = $fs.Read($buf, 0, $want)
            if ($script:csvUtf16 -and (($read % 2) -eq 1)) { $read = $read - 1 }
            $script:csvOffset = $script:csvOffset + $read
            if ($script:csvUtf16) { $chunk = [System.Text.Encoding]::Unicode.GetString($buf, 0, $read) }
            else                  { $chunk = [System.Text.Encoding]::UTF8.GetString($buf, 0, $read) }
        } finally { $fs.Dispose() }

        $text  = $script:csvRest + $chunk
        $lines = $text -split "`r?`n"
        $script:csvRest = $lines[$lines.Length - 1]

        for ($i = 0; $i -lt $lines.Length - 1; $i++) {
            $ln = $lines[$i]
            if ($ln.Length -lt 20) { continue }
            if ($ln.StartsWith('Application,')) { $script:csvHeader = $true; continue }
            $c = $ln.Split(',')
            if ($c.Length -lt 14) { continue }
            $t = 0.0; $dt = 0.0
            if (-not [double]::TryParse($c[7], [ref]$t))  { continue }
            if (-not [double]::TryParse($c[9], [ref]$dt)) { continue }
            if ($dt -le 0.2 -or $dt -gt 2000) { continue }
            # 加载/暂停造成的超长间隔统一按 250ms 计，避免单次卡顿毁掉 1% Low
            if ($dt -gt 250) { $dt = 250 }
            # Application 列区分来源：LS 的呈现帧 = 插帧后的实际输出帧率
            if ($c[0] -eq 'LosslessScaling.exe') {
                $script:LsFrames.Add($dt); $script:LsFrameT.Add($t)
                continue
            }
            $script:Frames.Add($dt)
            $script:FrameT.Add($t)
            if ($c[6] -eq '1') { $script:DropCnt++ }
            if ($c[11]) { $script:PresentMode = $c[11] }
        }
        # 只保留最近 1200 帧
        while ($script:Frames.Count -gt 1200) {
            $script:Frames.RemoveAt(0); $script:FrameT.RemoveAt(0)
        }
        while ($script:LsFrames.Count -gt 400) {
            $script:LsFrames.RemoveAt(0); $script:LsFrameT.RemoveAt(0)
        }
    } catch { }
}

# ---------------------------------------------------------------------
#  GPU 指标
# ---------------------------------------------------------------------
$script:Gpu = @{ Name = ''; Util = ''; Temp = ''; MemUsed = ''; MemTotal = ''; Power = ''; Clock = ''; Driver = '' }

function Read-GpuInfo {
    if (-not (Test-Path -LiteralPath $script:GpuPath)) { return }
    try {
        $fs = [System.IO.File]::Open($script:GpuPath, 'Open', 'Read', 'ReadWrite')
        try {
            $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)
            $txt = $sr.ReadToEnd()
        } finally { $fs.Dispose() }
        $lines = @($txt -split "`r?`n" | Where-Object { $_.Trim().Length -gt 0 })
        if ($lines.Count -eq 0) { return }
        $c = $lines[$lines.Count - 1].Split(',')
        if ($c.Length -ge 8) {
            $script:Gpu.Name     = $c[0].Trim()
            $script:Gpu.Util     = $c[1].Trim()
            $script:Gpu.Temp     = $c[2].Trim()
            $script:Gpu.MemUsed  = $c[3].Trim()
            $script:Gpu.MemTotal = $c[4].Trim()
            $script:Gpu.Power    = $c[5].Trim()
            $script:Gpu.Clock    = $c[6].Trim()
            $script:Gpu.Driver   = $c[7].Trim()
        }
    } catch { }
}

# ---------------------------------------------------------------------
#  CPU / 内存
# ---------------------------------------------------------------------
$script:PrevCpu = $null
$script:CpuPct  = 0
$script:CpuMhz  = 0
$script:MemTotal = 0
$script:MemUsed  = 0
$script:ProcMem  = 0

function Update-CpuMem {
    $t = [EdenOsdNative]::CpuTimes()
    if ($t) {
        if ($script:PrevCpu) {
            $dIdle = [double]($t[0] - $script:PrevCpu[0])
            $dKern = [double]($t[1] - $script:PrevCpu[1])
            $dUser = [double]($t[2] - $script:PrevCpu[2])
            $dTot  = $dKern + $dUser
            if ($dTot -gt 0) {
                $script:CpuPct = [Math]::Round((1.0 - ($dIdle / $dTot)) * 100.0, 0)
                if ($script:CpuPct -lt 0)   { $script:CpuPct = 0 }
                if ($script:CpuPct -gt 100) { $script:CpuPct = 100 }
            }
        }
        $script:PrevCpu = $t
    }
    $script:CpuMhz = [EdenOsdNative]::CurrentMhz()

    $m = [EdenOsdNative]::MemBytes()
    if ($m) {
        $script:MemTotal = [Math]::Round($m[0] / 1GB, 1)
        $script:MemUsed  = [Math]::Round(($m[0] - $m[1]) / 1GB, 1)
    }
}

# ---------------------------------------------------------------------
#  统计
# ---------------------------------------------------------------------
$script:Stat = @{ Fps = 0.0; Ms = 0.0; Low = 0.0; Frames1s = 0; Drop1s = 0; Valid = $false }
$script:PeakFps = 0.0

function Update-Stats {
    $n = $script:Frames.Count
    if ($n -lt 3) { $script:Stat.Valid = $false; $script:Stat.Fps = 0; $script:Stat.Ms = 0; $script:Stat.Low = 0; return }

    $now = $script:FrameT[$n - 1]
    $sum = 0.0; $cnt = 0
    for ($i = $n - 1; $i -ge 0; $i--) {
        if (($now - $script:FrameT[$i]) -gt 1.0) { break }
        $sum += $script:Frames[$i]; $cnt++
    }
    if ($cnt -gt 0) {
        $script:Stat.Frames1s = $cnt
        $script:Stat.Fps = [Math]::Round($cnt, 1)
        $script:Stat.Ms  = [Math]::Round($sum / $cnt, 2)
        $script:Stat.Valid = $true

        # 峰值 FPS（用于判断是否掉帧，缓慢衰减）
        if ($script:Stat.Fps -gt $script:PeakFps) { $script:PeakFps = $script:Stat.Fps }
        else { $script:PeakFps = [Math]::Max($script:Stat.Fps, $script:PeakFps - 0.15) }
    }

    # 1% Low：最近 600 帧里最差 1% 帧的平均 FPS
    $take = [Math]::Min(600, $n)
    $arr = New-Object 'System.Collections.Generic.List[double]'
    for ($i = $n - $take; $i -lt $n; $i++) { $arr.Add($script:Frames[$i]) }
    $arr.Sort()
    $arr.Reverse()
    $k = [Math]::Max(1, [int]($take * 0.01))
    $s = 0.0
    for ($i = 0; $i -lt $k; $i++) { $s += $arr[$i] }
    $worst = $s / $k
    if ($worst -gt 0) { $script:Stat.Low = [Math]::Round(1000.0 / $worst, 1) }
}

# Lossless Scaling 输出帧率：最近 1 秒内 LS 呈现了多少帧
function Update-LsStats {
    $n = $script:LsFrames.Count
    if ($n -lt 3) { $script:LsValid = $false; return }

    # 是否还在插帧：CSV 的 Time 列是同一数据时间轴，用「游戏最新帧 - LS 最新帧」
    # 判断。不能用墙钟：PresentMon 的 stdout 重定向按块刷新，文件长度会停几秒，
    # 墙钟判定会把正常工作的插帧误杀成 --。
    $lsT = $script:LsFrameT[$n - 1]
    $en  = $script:FrameT.Count
    if ($en -gt 0) {
        $eT = $script:FrameT[$en - 1]
        # 游戏还在出新帧、LS 却落后 2 秒以上 —— 插帧确实停了
        if (($eT - $lsT) -gt 2.0) { $script:LsValid = $false; return }
    }

    $cnt = 0
    for ($i = $n - 1; $i -ge 0; $i--) {
        if (($lsT - $script:LsFrameT[$i]) -gt 1.0) { break }
        $cnt++
    }
    if ($cnt -ge 2) {
        $script:LsFps = [Math]::Round($cnt, 1)
        $script:LsValid = $true
    } else {
        $script:LsValid = $false
    }
}

# ---------------------------------------------------------------------
#  界面
# ---------------------------------------------------------------------
$script:W = 356
$script:H = 236

# 高分屏（如 150% 缩放）：让进程直接按物理像素绘制，再整体放大画布，
# 避免系统对窗口做位图拉伸导致文字发虚
$script:Scale = 1.0
try {
    [void][EdenOsdNative]::SetProcessDPIAware()
    $gTmp = [System.Drawing.Graphics]::FromHwnd([IntPtr]::Zero)
    $script:Scale = $gTmp.DpiX / 96.0
    $gTmp.Dispose()
} catch { $script:Scale = 1.0 }
if ($script:Scale -lt 1.0) { $script:Scale = 1.0 }
$script:PW = [int]($script:W * $script:Scale)
$script:PH = [int]($script:H * $script:Scale)

$form = New-Object System.Windows.Forms.Form
$form.Text            = 'Eden OSD'
$form.FormBorderStyle = 'None'
$form.StartPosition   = 'Manual'
$form.ShowInTaskbar   = $false
$form.TopMost         = $true
$form.BackColor       = [System.Drawing.Color]::FromArgb(8, 10, 14)
$form.Opacity         = 0.88
$form.ClientSize      = New-Object System.Drawing.Size($script:PW, $script:PH)
$form.Location        = New-Object System.Drawing.Point(40, 40)

try {
    $bp = [System.Windows.Forms.Form].GetProperty('DoubleBuffered', [System.Reflection.BindingFlags]'Instance,NonPublic')
    $bp.SetValue($form, $true, $null)
} catch { }

# 圆角区域
try {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r = [int](9 * $script:Scale); $w0 = $script:PW; $h0 = $script:PH
    $path.AddArc(0, 0, $r * 2, $r * 2, 180, 90)
    $path.AddArc($w0 - $r * 2 - 1, 0, $r * 2, $r * 2, 270, 90)
    $path.AddArc($w0 - $r * 2 - 1, $h0 - $r * 2 - 1, $r * 2, $r * 2, 0, 90)
    $path.AddArc(0, $h0 - $r * 2 - 1, $r * 2, $r * 2, 90, 90)
    $path.CloseFigure()
    $form.Region = New-Object System.Drawing.Region($path)
} catch { }

# 字体 / 画笔
# 字体：按像素指定字号（等价 96 DPI 下的点数），交给 ScaleTransform 放大，
# 避免 DPI-aware 进程里 GDI+ 又按 144 DPI 放大一次导致文字溢出
$U = [System.Drawing.GraphicsUnit]::Pixel
$script:FTitle = New-Object System.Drawing.Font('Segoe UI', 11.3, [System.Drawing.FontStyle]::Bold, $U)
$script:FLabel = New-Object System.Drawing.Font('Microsoft YaHei UI', 11.3, [System.Drawing.FontStyle]::Regular, $U)
$script:FValue = New-Object System.Drawing.Font('Consolas', 12.7, [System.Drawing.FontStyle]::Bold, $U)
$script:FBig   = New-Object System.Drawing.Font('Consolas', 32, [System.Drawing.FontStyle]::Bold, $U)
$script:FSmall = New-Object System.Drawing.Font('Microsoft YaHei UI', 10, [System.Drawing.FontStyle]::Regular, $U)
$script:FUnit  = New-Object System.Drawing.Font('Consolas', 12, [System.Drawing.FontStyle]::Regular, $U)

$script:BTitle = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 120, 210, 255))
$script:BLabel = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 138, 147, 166))
$script:BValue = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 232, 236, 244))
$script:BDim   = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 104, 114, 132))
$script:BGood  = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 124, 227, 139))
$script:BMid   = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 245, 196, 81))
$script:BBad   = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 255, 107, 107))
$script:BBg    = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 8, 10, 14))
$script:BGraph = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(90, 120, 210, 255))
$script:BGraph2= New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(150, 245, 196, 81))
$script:BGraphBg = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(60, 20, 26, 36))
$script:PSep   = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(46, 130, 160, 200)), 1
$script:PEdge  = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(90, 120, 200, 255)), 1
$script:PRef   = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(80, 245, 196, 81)), 1

function Draw-Row {
    param($g, [string]$label, [string]$value, [int]$y, $brush)
    $g.DrawString($label, $script:FLabel, $script:BLabel, 12, $y)
    $sz = $g.MeasureString($value, $script:FValue)
    $g.DrawString($value, $script:FValue, $brush, ($script:W - 13 - $sz.Width), $y - 1)
}

$form.Add_Paint({
    param($sender, $e)
    try {
    $g  = $e.Graphics
    $g.SmoothingMode    = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
    if ($script:Scale -ne 1.0) { $g.ScaleTransform($script:Scale, $script:Scale) }

    $g.FillRectangle($script:BBg, 0, 0, $script:W, $script:H)
    $g.DrawRectangle($script:PEdge, 0, 0, $script:W - 1, $script:H - 1)

    # 标题
    $g.DrawString('EDEN OSD', $script:FTitle, $script:BTitle, 12, 8)
    $s2 = $g.MeasureString('F10 显示  F9 退出', $script:FSmall)
    $g.DrawString('F10 显示  F9 退出', $script:FSmall, $script:BDim, ($script:W - 13 - $s2.Width), 9)

    # FPS 大字
    $fpsTxt = if ($script:Stat.Valid) { $script:Stat.Fps.ToString('0.0') } else { '--' }
    $fpsBrush = $script:BValue
    if ($script:Stat.Valid) {
        $peak = $script:PeakFps
        if ($peak -le 0) { $peak = $script:Stat.Fps }
        if ($script:Stat.Fps -ge ($peak * 0.88)) { $fpsBrush = $script:BGood }
        elseif ($script:Stat.Fps -ge ($peak * 0.55)) { $fpsBrush = $script:BMid }
        else { $fpsBrush = $script:BBad }
    }
    $g.DrawString($fpsTxt, $script:FBig, $fpsBrush, 10, 20)
    $sw = $g.MeasureString($fpsTxt, $script:FBig)
    $g.DrawString('FPS', $script:FUnit, $script:BLabel, (10 + $sw.Width - 4), 38)
    $g.DrawString(([string]$script:RefreshHz + ' Hz'), $script:FSmall, $script:BDim, 84, 10)

    # 右侧：帧时间 / 1% Low
    $msTxt  = if ($script:Stat.Valid) { $script:Stat.Ms.ToString('0.00') + ' ms' } else { '--' }
    $lowTxt = if ($script:Stat.Low -gt 0) { $script:Stat.Low.ToString('0.0') + ' FPS' } else { '--' }
    $g.DrawString('帧时间', $script:FLabel, $script:BLabel, 176, 26)
    $g.DrawString($msTxt, $script:FValue, $script:BValue, 176, 40)
    $g.DrawString('1% Low', $script:FLabel, $script:BLabel, 278, 26)
    $g.DrawString($lowTxt, $script:FValue, $script:BValue, 278, 40)

    $g.DrawLine($script:PSep, 12, 66, $script:W - 12, 66)

    # CPU / GPU / 内存
    $cpuTxt = ('{0}%   {1:0.00} GHz' -f $script:CpuPct, ($script:CpuMhz / 1000.0))
    Draw-Row $g 'CPU' $cpuTxt 74 $script:BValue

    if ($script:Gpu.Name) {
        $mu = 0.0; $mt = 0.0; $pw = 0.0
        [void][double]::TryParse($script:Gpu.MemUsed, [ref]$mu)
        [void][double]::TryParse($script:Gpu.MemTotal, [ref]$mt)
        [void][double]::TryParse($script:Gpu.Power, [ref]$pw)
        $gpuTxt = ('{0}%   {1}C   {2:0.0}/{3:0.0}G   {4:0}W' -f `
            $script:Gpu.Util, $script:Gpu.Temp, ($mu / 1024), ($mt / 1024), $pw)
        Draw-Row $g 'GPU' $gpuTxt 92 $script:BValue
    } else {
        Draw-Row $g 'GPU' '不可用' 92 $script:BDim
    }

    $memTxt = ('{0:0.0}/{1:0.0} GB' -f $script:MemUsed, $script:MemTotal)
    Draw-Row $g '内存' $memTxt 110 $script:BValue

    # Lossless Scaling 输出帧率（只读）。LS 以管理员权限运行，
    # OSD 未提权时采集不到，直接提示而不是显示误导性的数值
    if ($script:LsValid) {
        Draw-Row $g 'LS 输出' ($script:LsFps.ToString('0.0') + ' FPS') 128 $script:BGood
    } elseif ($script:LsRunning -and -not $script:IsElevated) {
        Draw-Row $g 'LS 输出' '需管理员' 128 $script:BDim
    } else {
        Draw-Row $g 'LS 输出' '--' 128 $script:BDim
    }

    $g.DrawLine($script:PSep, 12, 148, $script:W - 12, 148)

    # 机器内核信息
    $line1 = $script:CpuModel + '  ·  ' + $script:Cores + 'C/' + $script:Threads + 'T'
    if ($line1.Length -gt 58) { $line1 = $line1.Substring(0, 58) }
    $g.DrawString($line1, $script:FSmall, $script:BDim, 12, 154)

    $line2 = $script:WinBuild
    if ($script:Gpu.Name) { $line2 = $script:Gpu.Name + '  ·  驱动 ' + $script:Gpu.Driver + '  ·  ' + $line2 }
    if ($script:PresentMode) { $line2 = $line2 + '  ·  ' + $script:PresentMode }
    if ($line2.Length -gt 62) { $line2 = $line2.Substring(0, 62) }
    $g.DrawString($line2, $script:FSmall, $script:BDim, 12, 168)

    # 帧时间曲线
    $gx = 12; $gy = 186; $gw = $script:W - 24; $gh = 40
    $g.FillRectangle($script:BGraphBg, $gx, $gy, $gw, $gh)
    # 参考线 = 当前峰值帧率对应的帧时间（游戏实际渲染节奏，而非显示器刷新率）
    $refMs = 1000.0 / $script:RefreshHz
    if ($script:PeakFps -gt 1) { $refMs = 1000.0 / $script:PeakFps }
    # 纵轴随参考帧时间自适应，低帧时也看得清柱形起伏
    $scaleMs = $refMs * 2.0
    if ($scaleMs -lt 8)  { $scaleMs = 8 }
    if ($scaleMs -gt 50) { $scaleMs = 50 }
    $refY = $gy + $gh - [int]($refMs / $scaleMs * $gh)
    if ($refY -lt $gy) { $refY = $gy }
    if ($refY -gt ($gy + $gh)) { $refY = $gy + $gh }
    $g.DrawLine($script:PRef, $gx, $refY, $gx + $gw, $refY)

    $cnt = $script:Frames.Count
    if ($cnt -gt 1) {
        $show = [Math]::Min($cnt, [int]($gw / 2))
        $start = $cnt - $show
        for ($i = 0; $i -lt $show; $i++) {
            $ms = $script:Frames[$start + $i]
            $hh = [int]($ms / $scaleMs * $gh)
            if ($hh -lt 1) { $hh = 1 }
            if ($hh -gt $gh) { $hh = $gh }
            $b = $script:BGraph
            if ($ms -gt ($refMs * 1.4)) { $b = $script:BGraph2 }
            $g.FillRectangle($b, ($gx + $i * 2), ($gy + $gh - $hh), 1, $hh)
        }
    }
    } catch { Write-OsdErr 'Paint' $_.Exception.Message }
})

# 点击穿透 + 不抢焦点 + 工具窗口 + 置顶
$form.Add_Shown({
    $ex = [EdenOsdNative]::GetWindowLong($form.Handle, -20)
    $ex = $ex -bor 0x00080000 -bor 0x00000020 -bor 0x08000000 -bor 0x00000080 -bor 0x00000008
    [void][EdenOsdNative]::SetWindowLong($form.Handle, -20, $ex)
    # WinForms 的 TopMost 有时留不住置顶位，这里显式置顶一次
    [void][EdenOsdNative]::SetWindowPos($form.Handle, [IntPtr](-1), 0, 0, 0, 0, 0x0213)
})

# ---------------------------------------------------------------------
#  主循环
# ---------------------------------------------------------------------
$script:GameHwnd   = [IntPtr]::Zero
$script:UserHidden = $false
$script:KeyF10Last = $false
$script:KeyQuitLast = $false
$script:LastTopTick = [DateTime]::UtcNow
$script:LastGpuTick = [DateTime]::MinValue
$script:GpuFail = 0

function Get-GameHwnd {
    $p = Get-Process -Name 'eden-cli' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $p) { $script:GameProc = $null; return [IntPtr]::Zero }
    $script:GameProc = $p
    if ($p.MainWindowHandle -ne [IntPtr]::Zero) { return $p.MainWindowHandle }
    return [IntPtr]::Zero
}

function Update-Position {
    $hwnd = $script:GameHwnd
    if ($hwnd -eq [IntPtr]::Zero) { return }
    if ([EdenOsdNative]::IsIconic($hwnd)) { if ($form.Visible) { $form.Hide() }; return }
    $rc = [EdenOsdNative+RECT]::new()
    if (-not [EdenOsdNative]::GetClientRect($hwnd, [ref]$rc)) { return }
    $pt = [EdenOsdNative+POINT]::new()
    $pt.X = 0; $pt.Y = 0
    if (-not [EdenOsdNative]::ClientToScreen($hwnd, [ref]$pt)) { return }
    $x = $pt.X + $rc.Right - $script:PW - [int](10 * $script:Scale)
    # 窗口比面板还窄时退回左上角，避免面板跑出画面
    if ($x -lt ($pt.X + [int](10 * $script:Scale))) { $x = $pt.X + [int](10 * $script:Scale) }
    $y = $pt.Y + [int](10 * $script:Scale)
    if ($form.Left -ne $x -or $form.Top -ne $y) { $form.Location = New-Object System.Drawing.Point($x, $y) }
    # 每 2 秒重申一次置顶，防止被其它全屏/置顶窗口压下去
    if ($form.Visible -and (([DateTime]::UtcNow - $script:LastTopTick).TotalMilliseconds -gt 2000)) {
        [void][EdenOsdNative]::SetWindowPos($form.Handle, [IntPtr](-1), 0, 0, 0, 0, 0x0213)
        $script:LastTopTick = [DateTime]::UtcNow
    }
}

$script:Tick = {
  try {
    # ---- 找游戏窗口 ----
    $hwnd = Get-GameHwnd
    $script:GameHwnd = $hwnd

    # ---- 前台判定（热键与显示门控都用它）----
    $fgPid = [uint32]0
    $fg = [EdenOsdNative]::GetForegroundWindow()
    if ($fg -ne [IntPtr]::Zero) { [void][EdenOsdNative]::GetWindowThreadProcessId($fg, [ref]$fgPid) }
    $active = ($script:GameProc -and $fgPid -eq [uint32]$script:GameProc.Id)

    # ---- 热键 ----
    # F10 只在游戏处于前台时切换，避免在别的窗口误按后一直看不到面板
    $k10 = (([int][EdenOsdNative]::GetAsyncKeyState(0x79)) -band 0x8000) -ne 0
    if ($k10 -and -not $script:KeyF10Last -and $active) {
        $script:UserHidden = -not $script:UserHidden
        if ($script:UserHidden) { if ($form.Visible) { $form.Hide() } }
        else { if (-not $form.Visible) { $form.Show() } }
    }
    $script:KeyF10Last = $k10

    $kQ = (([int][EdenOsdNative]::GetAsyncKeyState(0x78)) -band 0x8000) -ne 0
    if ($kQ -and -not $script:KeyQuitLast) { $script:Quit = $true }
    $script:KeyQuitLast = $kQ

    if ($hwnd -eq [IntPtr]::Zero) {
        if ($form.Visible) { $form.Hide() }
        Stop-OsdChild
        $script:Frames.Clear(); $script:FrameT.Clear()
        $script:LsFrames.Clear(); $script:LsFrameT.Clear()
        $script:Stat.Valid = $false
        $script:LsValid = $false
        $script:csvHeader = $false
        return
    }

    # ---- 子进程守护 ----
    if (-not $script:Pmon -or $script:Pmon.HasExited) {
        $script:Pmon = $null
        Start-PresentMon
    }
    if (-not $script:GpuPs -or $script:GpuPs.HasExited) {
        $script:GpuPs = $null
        Start-GpuLoop
    }

    # ---- 采集 ----
    Read-NewFrames
    if (([DateTime]::UtcNow - $script:LastGpuTick).TotalMilliseconds -gt 900) {
        Read-GpuInfo
        $script:LsRunning = [bool](Get-Process -Name 'LosslessScaling' -ErrorAction SilentlyContinue)
        $script:LastGpuTick = [DateTime]::UtcNow
    }
    Update-CpuMem
    Update-Stats
    Update-LsStats

    if ($script:GameProc) {
        try { $script:ProcMem = [Math]::Round($script:GameProc.WorkingSet64 / 1GB, 2) } catch { }
    }

    # ---- 显示 ----
    # 只在模拟器窗口处于前台时显示，避免面板浮在别的程序上面
    if ((-not $script:UserHidden) -and $active) {
        if (-not $form.Visible) { $form.Show() }
        Update-Position
        $form.Invalidate()
    } elseif ($form.Visible) {
        $form.Hide()
    }
  } catch { Write-OsdErr 'Tick' $_.Exception.Message }
}

$script:Quit = $false

Reset-OsdChild

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = $Interval
$timer.Add_Tick($script:Tick)
$timer.Start()

$form.Add_FormClosing({ Stop-OsdChild })

Write-OsdLog ('OSD 已启动' + $(if ($script:IsElevated) { ' (管理员)' } else { ' (非管理员, LS 帧率不可用)' }))
[void]$form.Show()
try { $form.Activate() } catch { }

while (-not $script:Quit) {
    [System.Windows.Forms.Application]::DoEvents()
    Start-Sleep -Milliseconds 25
}

$timer.Stop()
Stop-OsdChild
try { $form.Close() } catch { }
try { logman stop EdenOsd -ets | Out-Null } catch { }
Write-OsdLog 'OSD 已退出'
