using System.Runtime.InteropServices;
using System.Text;

namespace NCMMini;

internal static class NativeMethods
{
    private const uint WmClose = 0x0010;
    private const uint WmCommand = 0x0111;
    private const uint CloudMusicCommandCode = 0x1800;
    private const uint SmtoAbortIfHung = 0x0002;

    private delegate bool EnumWindowsCallback(nint window, nint parameter);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsCallback callback, nint parameter);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(nint window, StringBuilder text, int maxCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(nint window, StringBuilder text, int maxCount);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(nint window, out uint processId);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(nint window);

    [DllImport("user32.dll")]
    private static extern bool PostMessage(nint window, uint message, nint wParam, nint lParam);

    [DllImport("user32.dll")]
    private static extern nint SendMessageTimeout(
        nint window,
        uint message,
        nint wParam,
        nint lParam,
        uint flags,
        uint timeout,
        out nint result);

    public static IReadOnlyList<WindowInfo> EnumerateWindows()
    {
        var windows = new List<WindowInfo>();
        EnumWindows((window, _) =>
        {
            GetWindowThreadProcessId(window, out var processId);
            windows.Add(new WindowInfo(
                window,
                processId,
                ReadClassName(window),
                ReadWindowText(window),
                IsWindowVisible(window)));
            return true;
        }, 0);
        return windows;
    }

    public static bool SendCloudMusicCommand(uint processId, int slot)
    {
        var window = EnumerateWindows()
            .FirstOrDefault(candidate => candidate.ProcessId == processId && candidate.ClassName == "icon")
            ?.Handle ?? 0;
        if (window == 0)
        {
            return false;
        }

        var wParam = unchecked((nint)(((long)CloudMusicCommandCode << 16) | (uint)slot));
        return SendMessageTimeout(window, WmCommand, wParam, 0, SmtoAbortIfHung, 1000, out _) != 0;
    }

    public static bool CloseWindow(nint window) => window != 0 && PostMessage(window, WmClose, 0, 0);

    private static string ReadClassName(nint window)
    {
        var text = new StringBuilder(256);
        return GetClassName(window, text, text.Capacity) > 0 ? text.ToString() : string.Empty;
    }

    private static string ReadWindowText(nint window)
    {
        var text = new StringBuilder(1024);
        return GetWindowText(window, text, text.Capacity) > 0 ? text.ToString() : string.Empty;
    }
}

internal sealed record WindowInfo(nint Handle, uint ProcessId, string ClassName, string Title, bool IsVisible);

