using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing;
using System.Net.Http;
using System.Runtime.InteropServices;

namespace NCMMini;

internal static class CoverLoader
{
    public const int Width = 40;
    public const int Height = 40;
    private static readonly HttpClient Client = new() { Timeout = TimeSpan.FromSeconds(8) };

    public static async Task<byte[]> LoadAsync(string? url, CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(url))
        {
            return [];
        }

        var normalized = url.StartsWith("//", StringComparison.Ordinal)
            ? $"https:{url}"
            : url.StartsWith("http://", StringComparison.OrdinalIgnoreCase)
                ? $"https://{url[7..]}"
                : url;
        if (!Uri.TryCreate(normalized, UriKind.Absolute, out var uri))
        {
            return [];
        }

        try
        {
            using var request = new HttpRequestMessage(HttpMethod.Get, uri);
            request.Headers.UserAgent.ParseAdd("NCM-Mini/0.1");
            using var response = await Client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
            response.EnsureSuccessStatusCode();
            await using var stream = await response.Content.ReadAsStreamAsync(cancellationToken);
            using var source = Image.FromStream(stream);
            using var bitmap = new Bitmap(Width, Height, PixelFormat.Format32bppPArgb);
            using (var graphics = Graphics.FromImage(bitmap))
            {
                graphics.CompositingMode = CompositingMode.SourceCopy;
                graphics.CompositingQuality = CompositingQuality.HighQuality;
                graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
                graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
                graphics.DrawImage(source, new Rectangle(0, 0, Width, Height));
            }

            var pixels = new byte[Width * Height * 4];
            var data = bitmap.LockBits(new Rectangle(0, 0, Width, Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
            try
            {
                for (var row = 0; row < Height; row++)
                {
                    Marshal.Copy(data.Scan0 + row * data.Stride, pixels, row * Width * 4, Width * 4);
                }
            }
            finally
            {
                bitmap.UnlockBits(data);
            }
            return pixels;
        }
        catch (Exception exception) when (exception is HttpRequestException or ArgumentException or ExternalException or IOException)
        {
            Log.Write($"failed to load cover: {exception.Message}");
            return [];
        }
    }
}
