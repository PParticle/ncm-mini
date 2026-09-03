namespace NCMMini;

internal sealed class HostApplication
{
    private readonly AppOptions _options;
    private readonly PlayerController _player;
    private readonly TrackCatalog _catalog = new();
    private readonly LyricsStore _lyrics = new();
    private readonly CancellationTokenSource _shutdown = new();
    private uint _processId;

    public HostApplication(AppOptions options)
    {
        _options = options;
        _player = new PlayerController(options);
    }

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, _shutdown.Token);
        await using var pipe = new PipeServer(HandleCommandAsync);
        var pipeTask = pipe.RunAsync(linked.Token);
        DeskBandController.Run("show");

        if (_options.LaunchCloudMusic)
        {
            _player.TryLaunch();
        }
        var launchDeadline = DateTime.UtcNow.AddSeconds(20);
        var playerSeen = false;
        string previousTitle = string.Empty;
        string previousLyric = string.Empty;
        string publishedTitle = string.Empty;
        TrackInfo? track = null;
        IReadOnlyList<LyricLine> lyrics = [];
        byte[] cover = [];
        var trackStarted = DateTimeOffset.UtcNow;

        try
        {
            while (!linked.IsCancellationRequested)
            {
                var snapshot = _player.ReadSnapshot();
                _processId = snapshot.ProcessId;
                if (!snapshot.IsRunning)
                {
                    await pipe.PublishAsync(BandState.Disconnected, linked.Token);
                    if (playerSeen || _options.LaunchCloudMusic && DateTime.UtcNow >= launchDeadline)
                    {
                        break;
                    }
                    await Task.Delay(500, linked.Token);
                    continue;
                }

                playerSeen = true;
                if (!string.Equals(snapshot.WindowTitle, previousTitle, StringComparison.Ordinal))
                {
                    previousTitle = snapshot.WindowTitle;
                    track = _catalog.Find(snapshot.WindowTitle) ?? snapshot.Track;
                    trackStarted = DateTimeOffset.UtcNow;
                    lyrics = _options.ShowLyrics && track is not null ? _lyrics.Find(track) : [];
                    cover = track is null ? [] : await CoverLoader.LoadAsync(track.CoverUrl, linked.Token);
                    previousLyric = string.Empty;
                }

                var currentLyric = _options.ShowLyrics
                    ? LyricsStore.GetCurrent(lyrics, DateTimeOffset.UtcNow - trackStarted)
                    : string.Empty;
                if (!string.Equals(currentLyric, previousLyric, StringComparison.Ordinal)
                    || !string.Equals(snapshot.WindowTitle, publishedTitle, StringComparison.Ordinal))
                {
                    previousLyric = currentLyric;
                    publishedTitle = snapshot.WindowTitle;
                    var shownTrack = track ?? snapshot.Track;
                    await pipe.PublishAsync(new BandState(
                        true,
                        shownTrack?.Name ?? "网易云音乐",
                        shownTrack?.Artist ?? string.Empty,
                        currentLyric,
                        cover), linked.Token);
                }
                await Task.Delay(300, linked.Token);
            }
        }
        catch (OperationCanceledException) when (linked.IsCancellationRequested)
        {
        }
        finally
        {
            linked.Cancel();
            if (_options.CloseCloudMusicOnExit)
            {
                await _player.CloseAsync();
            }
            DeskBandController.Run("hide");
            try
            {
                await pipeTask;
            }
            catch (OperationCanceledException)
            {
            }
        }
    }

    private Task HandleCommandAsync(BandCommand command)
    {
        if (command == BandCommand.Exit)
        {
            _shutdown.Cancel();
        }
        else if (_processId != 0 && !_player.Send(command, _processId))
        {
            Log.Write($"CloudMusic command failed: {command}");
        }
        return Task.CompletedTask;
    }
}
