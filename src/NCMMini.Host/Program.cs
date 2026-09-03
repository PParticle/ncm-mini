namespace NCMMini;

internal static class Program
{
    [STAThread]
    private static async Task<int> Main(string[] args)
    {
        var options = AppOptions.Parse(args);
        using var instance = new EventWaitHandle(false, EventResetMode.ManualReset, @"Local\NCMMini.Host", out var isFirstInstance);
        if (!isFirstInstance)
        {
            if (options.LaunchCloudMusic)
            {
                new PlayerController(options).TryLaunch();
            }
            DeskBandController.Run("show");
            return 0;
        }

        using var shutdown = new CancellationTokenSource();
        AppDomain.CurrentDomain.ProcessExit += (_, _) => shutdown.Cancel();

        try
        {
            var application = new HostApplication(options);
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
