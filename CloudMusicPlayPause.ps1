Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class CloudMusicPlayPauseNative
{
    public delegate bool EnumWindowsCallback(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(IntPtr hWnd, StringBuilder text, int maxCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int maxCount);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [DllImport("user32.dll")]
    private static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam, uint flags, uint timeout, out IntPtr result);

    public static string ReadClassName(IntPtr hWnd)
    {
        var text = new StringBuilder(256);
        return GetClassName(hWnd, text, text.Capacity) > 0 ? text.ToString() : string.Empty;
    }

    public static string ReadWindowText(IntPtr hWnd)
    {
        var text = new StringBuilder(1024);
        return GetWindowText(hWnd, text, text.Capacity) > 0 ? text.ToString() : string.Empty;
    }

    public static bool SendCommand(IntPtr hWnd, int slot)
    {
        var wParam = unchecked((IntPtr)((0x1800L << 16) | (uint)slot));
        IntPtr result;
        return SendMessageTimeout(hWnd, 0x0111, wParam, IntPtr.Zero, 0x0002, 1000, out result) != IntPtr.Zero;
    }
}
'@

$commandName = "PlayPause"
$commandSlot = 1
$foundWindow = [IntPtr]::Zero
$foundProcessId = 0

foreach ($process in @(Get-Process -Name cloudmusic -ErrorAction SilentlyContinue)) {
    $candidateProcessId = [uint32]$process.Id
    $script:foundWindow = [IntPtr]::Zero
    [CloudMusicPlayPauseNative]::EnumWindows({
        param($hWnd, $lParam)
        [uint32]$windowProcessId = 0
        [void][CloudMusicPlayPauseNative]::GetWindowThreadProcessId($hWnd, [ref]$windowProcessId)
        if ($windowProcessId -eq $candidateProcessId -and [CloudMusicPlayPauseNative]::ReadClassName($hWnd) -eq "icon") {
            $script:foundWindow = $hWnd
            return $false
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null
    if ($script:foundWindow -ne [IntPtr]::Zero) {
        $foundWindow = $script:foundWindow
        $foundProcessId = $candidateProcessId
        break
    }
}

if ($foundWindow -eq [IntPtr]::Zero) {
    Write-Error "CloudMusic icon window was not found."
    exit 1
}

$before = [CloudMusicPlayPauseNative]::ReadWindowText($foundWindow)
$sent = [CloudMusicPlayPauseNative]::SendCommand($foundWindow, $commandSlot)
Start-Sleep -Milliseconds 750
$after = [CloudMusicPlayPauseNative]::ReadWindowText($foundWindow)

"command=$commandName pid=$foundProcessId hwnd=$('0x{0:X}' -f $foundWindow.ToInt64()) sent=$sent"
"before=$before"
"after=$after"
if (-not $sent) { exit 1 }
