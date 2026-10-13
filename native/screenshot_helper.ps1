param(
    [string]$OutputFile = "screenshot.png",
    [string]$WindowName = ""
)

Add-Type -AssemblyName System.Windows.Forms,System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll")]
    public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("user32.dll")]
    public static extern IntPtr GetDC(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }
}
"@

if ($WindowName -ne "") {
    $hwnd = [Win32]::FindWindow($null, $WindowName)
    if ($hwnd -eq [IntPtr]::Zero) {
        # try by class name
        $hwnd = [Win32]::FindWindow($WindowName, $null)
    }
    if ($hwnd -ne [IntPtr]::Zero) {
        $rect = New-Object Win32+RECT
        [Win32]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
        $w = [Math]::Max(10, $rect.Right - $rect.Left)
        $h = [Math]::Max(10, $rect.Bottom - $rect.Top)
        $bmp = New-Object System.Drawing.Bitmap($w, $h)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        $ok = [Win32]::PrintWindow($hwnd, $hdc, 2) # PW_RENDERFULLCONTENT
        if (-not $ok) {
            $ok = [Win32]::PrintWindow($hwnd, $hdc, 0)
        }
        $g.ReleaseHdc($hdc)
        $bmp.Save($OutputFile, [System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose()
        $bmp.Dispose()
        Write-Host "Window $WindowName captured ($w x $h) via PrintWindow: $ok"
        exit 0
    } else {
        Write-Host "Window '$WindowName' not found"
        exit 1
    }
}

$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap($b.Width, $b.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
$bmp.Save($OutputFile, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$bmp.Dispose()
Write-Host "Entire screen captured ($($b.Width) x $($b.Height)) to $OutputFile"
