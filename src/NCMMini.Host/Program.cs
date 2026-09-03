namespace NCMMini;

internal static class Program
{
    [STAThread]
    private static async Task<int> Main(string[] args)
    {
        using var instance = new Mutex(true, @"Local\NCMMini.Host", out var ownsMutex);
        if (!ownsMutex)
        {
            DeskBandController.Run("show");
            return 0;
        }

        using var shutdown = new CancellationTokenSource();
        AppDomain.CurrentDomain.ProcessExit += (_, _) => shutdown.Cancel();

        try
        {
            var application = new HostApplication(AppOptions.Parse(args));
            await application.RunAsync(shutdown.Token);
            return 0;
        }
        catch (Exception exception)
        {
            Log.Write(exception.ToString());
            return 1;
        }
    }
}

