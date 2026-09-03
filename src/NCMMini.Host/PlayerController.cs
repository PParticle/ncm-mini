using System.Diagnostics;

namespace NCMMini;

internal sealed class PlayerController
{
    private readonly AppOptions _options;
    private Process? _launchedProcess;

    public PlayerController(AppOptions options)
    {
        _options = options;
    }

    public bool TryLaunch()
    {
        if (ReadSnapshot().IsRunning)
        {
            return true;
        }

        var path = ResolvePath();
        if (path is null)
        {
            Log.Write("cloudmusic.exe was not found");
            return false;
        }

        try
        {
            _launchedProcess = Process.Start(new ProcessStartInfo
            {
                FileName = path,
                WorkingDirectory = Path.GetDirectoryName(path)!,
                UseShellExecute = true
            });
            Log.Write($"started CloudMusic: {path}");
            return _launchedProcess is not null;
        }
        catch (Exception exception) when (exception is InvalidOperationException or System.ComponentModel.Win32Exception)
        {
            Log.Write($"failed to start CloudMusic: {exception.Message}");
            return false;
        }
    }

    public PlayerSnapshot ReadSnapshot()
    {
        var processIds = Process.GetProcessesByName("cloudmusic")
            .Select(process =>
            {
                var id = process.Id;
                process.Dispose();
                return (uint)id;
            })
            .ToHashSet();
        if (processIds.Count == 0)
        {
            return PlayerSnapshot.Empty;
        }

        var windows = NativeMethods.EnumerateWindows()
            .Where(window => processIds.Contains(window.ProcessId))
            .ToArray();
        var mainWindow = windows.FirstOrDefault(window => window.ClassName == "OrpheusBrowserHost")
            ?? windows.FirstOrDefault(window => window.ClassName == "icon")
            ?? windows.FirstOrDefault(window => !string.IsNullOrWhiteSpace(window.Title));
        if (mainWindow is null)
        {
            return PlayerSnapshot.Empty;
        }

        var titleWindow = windows.FirstOrDefault(window =>
                window.ProcessId == mainWindow.ProcessId
                && window.ClassName == "OrpheusBrowserHost"
                && !string.IsNullOrWhiteSpace(window.Title))
            ?? windows.FirstOrDefault(window =>
                window.ProcessId == mainWindow.ProcessId
                && window.ClassName == "icon"
                && !string.IsNullOrWhiteSpace(window.Title));
        var title = titleWindow?.Title.Trim() ?? string.Empty;
        var parsed = PlayerTitle.Parse(title);
        var track = string.IsNullOrWhiteSpace(parsed.Name)
            ? null
            : new TrackInfo(parsed.Name, parsed.Artist, null, null, null);
        return new PlayerSnapshot(true, mainWindow.ProcessId, mainWindow.Handle, title, track);
    }

    public bool Send(BandCommand command, uint processId)
    {
        var slot = command switch
        {
            BandCommand.Previous => 0,
            BandCommand.PlayPause => 1,
            BandCommand.Next => 2,
            _ => -1
        };
        return slot >= 0 && NativeMethods.SendCloudMusicCommand(processId, slot);
    }

    public async Task CloseAsync()
    {
        var snapshot = ReadSnapshot();
        if (!snapshot.IsRunning)
        {
            return;
        }

        NativeMethods.CloseWindow(snapshot.MainWindow);
        var deadline = DateTime.UtcNow.AddSeconds(3);
        while (DateTime.UtcNow < deadline)
        {
            await Task.Delay(100);
            if (!ReadSnapshot().IsRunning)
            {
                return;
            }
        }

        try
        {
            var process = _launchedProcess;
            if (process is null || process.HasExited)
            {
                process = Process.GetProcessById((int)snapshot.ProcessId);
            }
            process.Kill(entireProcessTree: true);
        }
        catch (Exception exception) when (exception is InvalidOperationException or ArgumentException or System.ComponentModel.Win32Exception)
        {
            Log.Write($"failed to stop CloudMusic: {exception.Message}");
        }
    }

    private string? ResolvePath()
    {
        var candidates = new[]
        {
            _options.CloudMusicPath,
            Environment.GetEnvironmentVariable("NETEASE_CLOUDMUSIC_PATH"),
            @"D:\Apps\Netease\CloudMusic\cloudmusic.exe",
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "NetEase", "CloudMusic", "cloudmusic.exe"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "NetEase", "CloudMusic", "cloudmusic.exe"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "NetEase", "CloudMusic", "cloudmusic.exe")
        };
        return candidates.FirstOrDefault(path => !string.IsNullOrWhiteSpace(path) && File.Exists(path));
    }
}

