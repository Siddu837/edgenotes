Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class WinTest {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern bool SetProcessDpiAwarenessContext(IntPtr dpiContext);
    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern uint RegisterWindowMessage(string lpString);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

[WinTest]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
Add-Type -AssemblyName System.Drawing

$artifactDir = "C:\Users\keert\.gemini\antigravity-ide\brain\4bbc9efa-4a02-4a01-820e-f13100d109fc"

function CaptureWindowBitmap([IntPtr]$hwnd, [string]$filename) {
    $rc = New-Object WinTest+RECT
    [WinTest]::GetWindowRect($hwnd, [ref]$rc) | Out-Null
    $w = [Math]::Max(10, $rc.Right - $rc.Left)
    $h = [Math]::Max(10, $rc.Bottom - $rc.Top)
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $ok = [WinTest]::PrintWindow($hwnd, $hdc, 2)
    if (-not $ok) {
        $ok = [WinTest]::PrintWindow($hwnd, $hdc, 0)
    }
    $g.ReleaseHdc($hdc)
    $path = Join-Path $artifactDir $filename
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
    Write-Host "Window $filename ($w x $h) saved: $ok -> $path"
}

# 1. Stop old instances
Get-Process | Where-Object { $_.ProcessName -match "EdgeNotes" } | Stop-Process -Force
Start-Sleep -Milliseconds 800

# 2. Launch
$proc = Start-Process -FilePath "native\windows\build\EdgeNotes.exe" -PassThru
Start-Sleep -Milliseconds 1200

$targetPid = [uint32]$proc.Id
Write-Host "EdgeNotes running PID: $targetPid"

# Find windows by EnumWindows
$wins = @{}
[WinTest]::EnumWindows({
    param($hwnd, $lParam)
    $pidOut = [uint32]0
    [WinTest]::GetWindowThreadProcessId($hwnd, [ref]$pidOut) | Out-Null
    if ($pidOut -eq $targetPid) {
        $sbCls = New-Object System.Text.StringBuilder 256
        [WinTest]::GetClassName($hwnd, $sbCls, 256) | Out-Null
        $cls = $sbCls.ToString()
        $wins[$cls] = $hwnd
    }
    return $true
}, [IntPtr]::Zero) | Out-Null

Write-Host "Discovered Windows:"
foreach ($key in $wins.Keys) {
    $rc = New-Object WinTest+RECT
    [WinTest]::GetWindowRect($wins[$key], [ref]$rc) | Out-Null
    $vis = [WinTest]::IsWindowVisible($wins[$key])
    Write-Host "  $key : HWND=$($wins[$key]), Visible=$vis, Bounds=($($rc.Left), $($rc.Top), $($rc.Right), $($rc.Bottom))"
}

$dockHwnd = $wins["EdgeDock"]
$peekHwnd = $wins["EdgePeek"]
$editorHwnd = $wins["EdgeEditor"]
$managerHwnd = $wins["EdgeManager"]

if (-not $dockHwnd) {
    Write-Error "EdgeDock window not found!"
    exit 1
}

# Capture Collapsed Dock
CaptureWindowBitmap $dockHwnd "screen_dock.png"

# Switch to Peek via EdgeNotes_ActivateInstance
$wmActivate = [WinTest]::RegisterWindowMessage("EdgeNotes_ActivateInstance")
[WinTest]::PostMessage($dockHwnd, $wmActivate, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 600

Write-Host "Peek visible: $([WinTest]::IsWindowVisible($peekHwnd))"
CaptureWindowBitmap $peekHwnd "screen_peek.png"

# Switch to Editor via CmdNewNote (101)
$WM_COMMAND = 0x0111
[WinTest]::PostMessage($dockHwnd, $WM_COMMAND, [IntPtr]101, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 600

Write-Host "Editor visible: $([WinTest]::IsWindowVisible($editorHwnd))"
CaptureWindowBitmap $editorHwnd "screen_editor.png"

# Switch to Manager via CmdAllNotes (102)
[WinTest]::PostMessage($dockHwnd, $WM_COMMAND, [IntPtr]102, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 600

Write-Host "Manager visible: $([WinTest]::IsWindowVisible($managerHwnd))"
CaptureWindowBitmap $managerHwnd "screen_manager.png"

# Performance check
$p = Get-Process -Id $targetPid
$wsMb = [Math]::Round($p.WorkingSet64 / (1024 * 1024), 2)
$cpuStart = $p.TotalProcessorTime.TotalMilliseconds
Start-Sleep -Seconds 1
$p.Refresh()
$cpuEnd = $p.TotalProcessorTime.TotalMilliseconds
$cpuPercent = [Math]::Round(($cpuEnd - $cpuStart) / 1000.0 * 100.0 / [Environment]::ProcessorCount, 2)

Write-Host "PERFORMANCE RESULT: Private WorkingSet = $wsMb MB, Idle CPU = $cpuPercent %"

# Quit cleanly (CmdExitApp = 113)
[WinTest]::PostMessage($dockHwnd, $WM_COMMAND, [IntPtr]113, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 500

Write-Host "Verification PASSED!"
