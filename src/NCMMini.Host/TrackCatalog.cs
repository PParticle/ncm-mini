using System.Text.Json;

namespace NCMMini;

internal sealed class TrackCatalog
{
    private readonly string _dataDirectory = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "NetEase",
        "CloudMusic",
        "webdata",
        "file");
    private DateTime _lastLoadUtc;
    private List<TrackInfo> _tracks = [];

    public TrackInfo? Find(string title)
    {
        var parsed = PlayerTitle.Parse(title);
        if (string.IsNullOrWhiteSpace(parsed.Name))
        {
            return null;
        }

        EnsureLoaded();
        var name = Normalize(parsed.Name);
        var artist = Normalize(parsed.Artist);
        return _tracks
            .Where(track => Normalize(track.Name) == name)
            .OrderByDescending(track => !string.IsNullOrWhiteSpace(artist) && Normalize(track.Artist).Contains(artist, StringComparison.Ordinal))
            .FirstOrDefault();
    }

    private void EnsureLoaded()
    {
        if ((DateTime.UtcNow - _lastLoadUtc).TotalSeconds < 5)
        {
            return;
        }

        _lastLoadUtc = DateTime.UtcNow;
        var tracks = new Dictionary<string, TrackInfo>(StringComparer.OrdinalIgnoreCase);
        foreach (var path in GetCandidatePaths())
        {
            TryReadTracks(path, tracks);
        }
        _tracks = tracks.Values.ToList();
    }

    private IEnumerable<string> GetCandidatePaths()
    {
        if (!Directory.Exists(_dataDirectory))
        {
            yield break;
        }

        IEnumerable<string> paths;
        try
        {
            paths = Directory.EnumerateFiles(_dataDirectory, "*", SearchOption.AllDirectories)
                .Where(path => new FileInfo(path).Length <= 16 * 1024 * 1024)
                .OrderByDescending(File.GetLastWriteTimeUtc)
                .Take(96)
                .ToArray();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            yield break;
        }

        foreach (var path in paths)
        {
            yield return path;
        }
    }

    private static void TryReadTracks(string path, IDictionary<string, TrackInfo> tracks)
    {
        try
        {
            var text = File.ReadAllText(path);
            if (text.Length == 0)
            {
                return;
            }

            using var document = JsonDocument.Parse(text);
            Visit(document.RootElement, tracks);
        }
        catch (Exception exception) when (exception is JsonException or IOException or UnauthorizedAccessException)
        {
        }
    }

    private static void Visit(JsonElement element, IDictionary<string, TrackInfo> tracks)
    {
        if (element.ValueKind == JsonValueKind.Object)
        {
            var track = TryCreateTrack(element);
            if (track is not null)
            {
                var key = track.TrackId ?? $"{track.Name}\0{track.Artist}";
                tracks[key] = track;
            }
            foreach (var property in element.EnumerateObject())
            {
                Visit(property.Value, tracks);
            }
        }
        else if (element.ValueKind == JsonValueKind.Array)
        {
            foreach (var item in element.EnumerateArray())
            {
                Visit(item, tracks);
            }
        }
    }

    private static TrackInfo? TryCreateTrack(JsonElement element)
    {
        var name = GetString(element, "name");
        if (string.IsNullOrWhiteSpace(name))
        {
            return null;
        }

        var album = GetObject(element, "album") ?? GetObject(element, "al");
        var coverUrl = album is null
            ? GetFirstString(element, "picUrl", "coverImgUrl", "coverUrl", "blurPicUrl")
            : GetFirstString(album.Value, "picUrl", "coverImgUrl", "coverUrl", "blurPicUrl");
        return new TrackInfo(
            name,
            ReadArtists(element),
            coverUrl,
            GetString(element, "id") ?? GetString(element, "trackId"),
            GetString(element, "lrcid") ?? GetString(element, "lyricsId") ?? GetString(element, "lyricId"));
    }

    private static string ReadArtists(JsonElement element)
    {
        if (!element.TryGetProperty("artists", out var artists) && !element.TryGetProperty("ar", out artists))
        {
            return GetString(element, "artist") ?? string.Empty;
        }
        if (artists.ValueKind == JsonValueKind.String)
        {
            return artists.GetString() ?? string.Empty;
        }
        if (artists.ValueKind != JsonValueKind.Array)
        {
            return string.Empty;
        }
        return string.Join("/", artists.EnumerateArray()
            .Select(item => item.ValueKind == JsonValueKind.Object ? GetString(item, "name") : item.GetString())
            .Where(value => !string.IsNullOrWhiteSpace(value)));
    }

    private static JsonElement? GetObject(JsonElement element, string name) =>
        element.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.Object ? value : null;

    private static string? GetString(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out var value))
        {
            return null;
        }
        return value.ValueKind switch
        {
            JsonValueKind.String => value.GetString(),
            JsonValueKind.Number => value.GetRawText(),
            _ => null
        };
    }

    private static string? GetFirstString(JsonElement element, params string[] names) =>
        names.Select(name => GetString(element, name)).FirstOrDefault(value => !string.IsNullOrWhiteSpace(value));

    private static string Normalize(string value) =>
        string.Join(' ', value.Trim().Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)).ToLowerInvariant();
}

