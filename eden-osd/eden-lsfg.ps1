param(
    [string]$LsPath = 'D:\Steam\steamapps\common\Lossless Scaling\LosslessScaling.exe',
    [int]$WaitTimeoutSec = 300
)
# Eden + Lossless Scaling launcher
# 1) wait until eden-cli has a main window
# 2) wait until that window is actually rendering (its title reports an FPS)
# 3) start Lossless Scaling if it is not running
# 4) bring Eden to the foreground, then send Ctrl+Alt+S
# 5) verify that Lossless Scaling created its scaling output window

Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class LsfgNative {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte k, byte s, uint f, UIntPtr e);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  // A background process is not allowed to steal the foreground; these are the
  // usual ways around the foreground lock.
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
  [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr h, bool altTab);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextLengthW(IntPtr h);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)]
  struct RECT { public int Left, Top, Right, Bottom; }
  // While scaling, Lossless Scaling shows borderless untitled output windows whose
  // class is "LosslessScaling" / "LosslessScalingBlk". The class name is the exact
  // signal; the size check stays only as a fallback for other builds.
  public static int CountScaleWindows(uint pid, int minW, int minH) {
    int count = 0;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint owner;
      GetWindowThreadProcessId(h, out owner);
      if (owner != pid) return true;
      if (!IsWindowVisible(h)) return true;
      if (GetWindowTextLengthW(h) > 0) return true;
      StringBuilder cls = new StringBuilder(128);
      GetClassNameW(h, cls, 128);
      if (cls.ToString().StartsWith("LosslessScaling")) { count++; return true; }
      RECT r;
      if (!GetWindowRect(h, out r)) return true;
      if ((r.Right - r.Left) >= minW && (r.Bottom - r.Top) >= minH) count++;
      return true;
    }, IntPtr.Zero);
    return count;
  }
}
"@

$log = Join-Path $PSScriptRoot 'eden-lsfg.log'
function Log([string]$m) {
    "$((Get-Date).ToString('yyyy-MM-dd HH:mm:ss')) $m" | Out-File -LiteralPath $log -Append -Encoding UTF8
}

function Get-GameFps([string]$title) {
    # Eden keeps its render FPS in the window title, e.g. "pokopia | ... | FPS: 60 (100%)"
    if ($title -match 'FPS:\s*(\d+)') { return [int]$Matches[1] }
    return -1
}

function Restore-GameWindow([IntPtr]$h) {
    # Lossless Scaling can only hook a visible game window; a minimized one sits at -21333
    if ([LsfgNative]::IsIconic($h)) {
        [void][LsfgNative]::ShowWindow($h, 9)
        Start-Sleep -Milliseconds 800
        return $true
    }
    return $false
}

function Set-GameForeground([IntPtr]$h, [int]$procId) {
    if ([LsfgNative]::GetForegroundWindow() -eq $h) { return $true }
    [void](Restore-GameWindow $h)
    try {
        $sh = New-Object -ComObject WScript.Shell
        [void]$sh.AppActivate($procId)
    } catch { }
    Start-Sleep -Milliseconds 200
    if ([LsfgNative]::GetForegroundWindow() -eq $h) { return $true }

    # A bare Alt tap tells the shell that this process received real input; that is
    # the documented way past the foreground lock, and it must happen first.
    [LsfgNative]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
    [LsfgNative]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60

    $fgPid = [uint32]0
    $fgWnd = [LsfgNative]::GetForegroundWindow()
    $tidFg = [LsfgNative]::GetWindowThreadProcessId($fgWnd, [ref]$fgPid)
    $tidSelf = [LsfgNative]::GetCurrentThreadId()
    $attached = $false
    if ($tidFg -ne 0 -and $tidFg -ne $tidSelf) {
        $attached = [LsfgNative]::AttachThreadInput($tidSelf, $tidFg, $true)
    }
    try {
        [void][LsfgNative]::ShowWindow($h, 9)
        # HWND_TOPMOST then HWND_NOTOPMOST is the second classic foreground trick
        [void][LsfgNative]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0013)  # SWP_NOSIZE|SWP_NOMOVE
        [void][LsfgNative]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x0013)
        [void][LsfgNative]::BringWindowToTop($h)
        [void][LsfgNative]::SetForegroundWindow($h)
        [void][LsfgNative]::SetFocus($h)
        [LsfgNative]::SwitchToThisWindow($h, $true)
    } finally {
        if ($attached) { [void][LsfgNative]::AttachThreadInput($tidSelf, $tidFg, $false) }
    }
    Start-Sleep -Milliseconds 400
    return ([LsfgNative]::GetForegroundWindow() -eq $h)
}

function Send-LsHotkey {
    # Ctrl + Alt + S
    [LsfgNative]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
    [LsfgNative]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
    [LsfgNative]::keybd_event(0x53, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 120
    [LsfgNative]::keybd_event(0x53, 0, 2, [UIntPtr]::Zero)
    [LsfgNative]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
    [LsfgNative]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
}

function Get-ScaleWindowCount {
    $ls = Get-Process -Name 'LosslessScaling' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $ls) { return -1 }
    # Keep this floor well below the game window: the scaling output window is
    # the same size as the game, and Eden can run at 868x517 or smaller. A
    # threshold of 1280x720 made every check report "not scaling" and then the
    # hotkey toggle turned a working frame generation back off.
    return [LsfgNative]::CountScaleWindows([uint32]$ls.Id, 640, 360)
}

function Set-LsCaptureApi {
    # DXGI cannot capture Eden's SDL window: Lossless Scaling still reports a
    # steady 240 fps output, but the captured frame is black, so no picture and
    # no FPS counter show up. WGC captures it correctly. Only touch the file
    # while Lossless Scaling is stopped, otherwise it overwrites it on exit.
    $cfg = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Lossless Scaling\Settings.xml'
    if (-not (Test-Path -LiteralPath $cfg)) { Log "LS settings not found: $cfg"; return }
    try {
        [xml]$x = Get-Content -LiteralPath $cfg -Raw
        $changed = $false
        foreach ($p in @($x.Settings.GameProfiles.Profile)) {
            if ($p -and $p.CaptureApi -and $p.CaptureApi -ne 'WGC') {
                Log "LS CaptureApi $($p.CaptureApi) -> WGC"
                $p.CaptureApi = 'WGC'
                $changed = $true
            }
        }
        if ($changed) { $x.Save($cfg) }
    } catch {
        Log "LS settings patch failed: $($_.Exception.Message)"
    }
}

Log 'started, waiting for eden-cli window'

$game = $null
for ($i = 0; $i -lt ($WaitTimeoutSec * 2); $i++) {
    $game = Get-Process eden-cli -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($game) { break }
    Start-Sleep -Milliseconds 500
}
if (-not $game) { Log 'eden-cli window not found within timeout, exit'; exit }
Log "eden-cli pid=$($game.Id) hwnd=$($game.MainWindowHandle)"

# Lossless Scaling hooks whatever window is in the foreground when the hotkey is
# pressed. Toggling during the loading screen makes it latch onto a black frame or
# onto the desktop, so wait until Eden reports a render frame-rate.
$fps = -1
for ($i = 0; $i -lt 90; $i++) {
    $game.Refresh()
    $fps = Get-GameFps $game.MainWindowTitle
    if ($fps -gt 0) { break }
    Start-Sleep -Seconds 1
}
if ($fps -gt 0) {
    Log "game is rendering (fps=$fps)"
} else {
    Log 'game title never reported an FPS, waiting 5s then continuing'
    Start-Sleep -Seconds 5
}

if (-not (Test-Path -LiteralPath $LsPath)) { Log "Lossless Scaling not found: $LsPath"; exit }

if (-not (Get-Process -Name 'LosslessScaling' -ErrorAction SilentlyContinue)) {
    Set-LsCaptureApi
    Log 'starting Lossless Scaling'
    Start-Process -FilePath $LsPath -WorkingDirectory (Split-Path -LiteralPath $LsPath)
    Start-Sleep -Seconds 8
} else {
    Log 'Lossless Scaling already running'
}

if ((Get-ScaleWindowCount) -gt 0) {
    Log 'frame generation already active, nothing to do'
    exit
}

$done = $false
for ($try = 1; $try -le 5; $try++) {
    $fg = $false
    for ($k = 0; $k -lt 8 -and -not $fg; $k++) {
        $fg = Set-GameForeground $game.MainWindowHandle $game.Id
        if (-not $fg) { Start-Sleep -Milliseconds 500 }
    }
    if (-not $fg) {
        # Sending the hotkey while another window owns the foreground would make
        # Lossless Scaling capture that window instead of the game.
        Log "attempt ${try}: game never became foreground, not sending hotkey"
        Start-Sleep -Seconds 2
        continue
    }
    Send-LsHotkey
    Start-Sleep -Seconds 4
    $n = Get-ScaleWindowCount
    if ($n -gt 0) {
        Log "Ctrl+Alt+S sent, frame generation on ($n scale window)"
        $done = $true
        break
    }
    Log "attempt ${try}: no scale window, retrying"
}
if (-not $done) { Log 'Ctrl+Alt+S could not enable frame generation, press it manually with Eden focused' }
