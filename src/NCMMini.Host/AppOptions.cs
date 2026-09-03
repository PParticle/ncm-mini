namespace NCMMini;

internal sealed record AppOptions(
    string? CloudMusicPath,
    bool LaunchCloudMusic,
    bool CloseCloudMusicOnExit,
    bool ShowLyrics)
{
    public static AppOptions Parse(string[] args)
    {
        string? cloudMusicPath = null;
        var launchCloudMusic = true;
        var closeCloudMusicOnExit = true;
        var showLyrics = true;

        for (var index = 0; index < args.Length; index++)
        {
            switch (args[index].ToLowerInvariant())
            {
                case "--cloudmusic":
                case "--player":
                    if (index + 1 < args.Length)
                    {
                        cloudMusicPath = args[++index];
                    }
                    break;
                case "--no-launch":
                    launchCloudMusic = false;
                    break;
                case "--keep-player":
                    closeCloudMusicOnExit = false;
                    break;
                case "--no-lyrics":
                    showLyrics = false;
                    break;
            }
        }

        return new AppOptions(cloudMusicPath, launchCloudMusic, closeCloudMusicOnExit, showLyrics);
    }
}

