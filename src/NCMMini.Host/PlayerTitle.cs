namespace NCMMini;

internal static class PlayerTitle
{
    public static (string Name, string Artist) Parse(string title)
    {
        var cleanTitle = title.Trim();
        var separator = cleanTitle.LastIndexOf(" - ", StringComparison.Ordinal);
        if (separator <= 0 || separator + 3 >= cleanTitle.Length)
        {
            return (cleanTitle, string.Empty);
        }

        return (cleanTitle[..separator].Trim(), cleanTitle[(separator + 3)..].Trim());
    }
}

