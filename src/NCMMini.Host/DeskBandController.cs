using System.Diagnostics;

namespace NCMMini;

internal static class DeskBandController
{
    public static bool Run(string command)
    {
        var controller = Path.Combine(AppContext.BaseDirectory, "NCMMiniBandCtl.exe");
        if (!File.Exists(controller))
        {
            Log.Write($"DeskBand controller was not found: {controller}");
            return false;
        }

        try
        {
            using var process = Process.Start(new ProcessStartInfo
            {
                FileName = controller,
                Arguments = command,
                UseShellExecute = false,
                CreateNoWindow = true
            });
            process?.WaitForExit(3000);
            return process?.ExitCode == 0;
        }
        catch (Exception exception) when (exception is InvalidOperationException or System.ComponentModel.Win32Exception)
        {
            Log.Write($"DeskBand {command} failed: {exception.Message}");
            return false;
        }
    }
}

