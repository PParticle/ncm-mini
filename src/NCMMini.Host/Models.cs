namespace NCMMini;

internal sealed record TrackInfo(
    string Name,
    string Artist,
    string? CoverUrl,
    string? TrackId,
    string? LyricsId);

internal sealed record PlayerSnapshot(
    bool IsRunning,
    uint ProcessId,
    nint MainWindow,
    string WindowTitle,
    TrackInfo? Track)
{
    public static PlayerSnapshot Empty { get; } = new(false, 0, 0, string.Empty, null);
}

internal sealed record LyricLine(TimeSpan Position, string Text);

internal sealed record BandState(
    bool IsRunning,
    string Title,
    string Artist,
    string Lyric,
    byte[] Cover)
{
    public static BandState Disconnected { get; } = new(false, "网易云音乐未连接", "正在等待客户端", string.Empty, []);
}

internal enum BandCommand : uint
{
    Previous = 1,
    PlayPause = 2,
    Next = 3,
    Exit = 4
}

