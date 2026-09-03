using System.Globalization;
using System.Text.RegularExpressions;

namespace NCMMini;

internal sealed partial class LyricsStore
{
    private readonly string _dataDirectory = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "NetEase",
        "CloudMusic",
        "webdata",
        "file");

    public IReadOnlyList<LyricLine> Find(TrackInfo track)
    {
        if (!Directory.Exists(_dataDirectory))
        {
            return [];
        }

        var identifiers = new[] { track.LyricsId, track.TrackId }
            .Where(value => !string.IsNullOrWhiteSpace(value))
            .ToArray();
        if (identifiers.Length == 0)
        {
            return [];
        }

        try
        {
            foreach (var path in Directory.EnumerateFiles(_dataDirectory, "*", SearchOption.AllDirectories)
                         .Where(path => identifiers.Any(id => Path.GetFileName(path).Contains(id!, StringComparison.OrdinalIgnoreCase)))
                         .Take(32))
            {
                try
                {
                    var result = Parse(File.ReadAllText(path));
                    if (result.Count > 0)
                    {
                        return result;
                    }
                }
                catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
                {
                }
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
        }
        return [];
    }

    public static IReadOnlyList<LyricLine> Parse(string text)
    {
        var lines = new List<LyricLine>();
        foreach (var rawLine in text.Split('\n'))
        {
            var match = Timestamp().Match(rawLine.TrimEnd('\r'));
            if (!match.Success)
            {
                continue;
            }
            var minutes = int.Parse(match.Groups["minutes"].Value, CultureInfo.InvariantCulture);
            var seconds = int.Parse(match.Groups["seconds"].Value, CultureInfo.InvariantCulture);
            var fraction = match.Groups["fraction"].Value.PadRight(3, '0');
            var milliseconds = fraction.Length == 0 ? 0 : int.Parse(fraction[..3], CultureInfo.InvariantCulture);
            lines.Add(new LyricLine(new TimeSpan(0, 0, minutes, seconds, milliseconds), match.Groups["text"].Value.Trim()));
        }
        return lines.OrderBy(line => line.Position).ToArray();
    }

    public static string GetCurrent(IReadOnlyList<LyricLine> lines, TimeSpan elapsed)
    {
        var current = string.Empty;
        foreach (var line in lines)
        {
            if (line.Position > elapsed)
            {
                break;
            }
            current = line.Text;
        }
        return current;
    }

    [GeneratedRegex(@"\[(?<minutes>\d{1,3}):(?<seconds>\d{2})(?:\.(?<fraction>\d{1,3}))?\](?<text>.*)")]
    private static partial Regex Timestamp();
}

