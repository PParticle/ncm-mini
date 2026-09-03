using System.Buffers.Binary;
using System.IO.Pipes;
using System.Text;

namespace NCMMini;

internal sealed class PipeServer : IAsyncDisposable
{
    private const uint Magic = 0x314D434E;
    private readonly Func<BandCommand, Task> _commandHandler;
    private readonly SemaphoreSlim _writeGate = new(1, 1);
    private readonly object _connectionGate = new();
    private NamedPipeServerStream? _connection;
    private BandState _state = BandState.Disconnected;

    public PipeServer(Func<BandCommand, Task> commandHandler)
    {
        _commandHandler = commandHandler;
    }

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            await using var server = new NamedPipeServerStream(
                "NCMMini-v1",
                PipeDirection.InOut,
                1,
                PipeTransmissionMode.Byte,
                PipeOptions.Asynchronous);
            try
            {
                await server.WaitForConnectionAsync(cancellationToken);
                lock (_connectionGate)
                {
                    _connection = server;
                }
                await SendStateAsync(_state, cancellationToken);
                await ReadCommandsAsync(server, cancellationToken);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                break;
            }
            catch (IOException exception)
            {
                Log.Write($"DeskBand pipe disconnected: {exception.Message}");
            }
            finally
            {
                lock (_connectionGate)
                {
                    if (ReferenceEquals(_connection, server))
                    {
                        _connection = null;
                    }
                }
            }
        }
    }

    public async Task PublishAsync(BandState state, CancellationToken cancellationToken)
    {
        _state = state;
        await SendStateAsync(state, cancellationToken);
    }

    public ValueTask DisposeAsync()
    {
        lock (_connectionGate)
        {
            _connection?.Dispose();
            _connection = null;
        }
        _writeGate.Dispose();
        return ValueTask.CompletedTask;
    }

    private async Task ReadCommandsAsync(Stream stream, CancellationToken cancellationToken)
    {
        var packet = new byte[12];
        while (!cancellationToken.IsCancellationRequested)
        {
            await stream.ReadExactlyAsync(packet, cancellationToken);
            if (BinaryPrimitives.ReadUInt32LittleEndian(packet) != Magic
                || BinaryPrimitives.ReadUInt16LittleEndian(packet.AsSpan(4)) != 1
                || BinaryPrimitives.ReadUInt16LittleEndian(packet.AsSpan(6)) != 2)
            {
                throw new IOException("invalid DeskBand command packet");
            }

            var command = (BandCommand)BinaryPrimitives.ReadUInt32LittleEndian(packet.AsSpan(8));
            if (Enum.IsDefined(command))
            {
                await _commandHandler(command);
            }
        }
    }

    private async Task SendStateAsync(BandState state, CancellationToken cancellationToken)
    {
        NamedPipeServerStream? connection;
        lock (_connectionGate)
        {
            connection = _connection;
        }
        if (connection is null || !connection.IsConnected)
        {
            return;
        }

        var packet = CreateStatePacket(state);
        await _writeGate.WaitAsync(cancellationToken);
        try
        {
            if (connection.IsConnected)
            {
                await connection.WriteAsync(packet, cancellationToken);
                await connection.FlushAsync(cancellationToken);
            }
        }
        catch (IOException)
        {
        }
        finally
        {
            _writeGate.Release();
        }
    }

    private static byte[] CreateStatePacket(BandState state)
    {
        var title = Encoding.UTF8.GetBytes(state.Title);
        var artist = Encoding.UTF8.GetBytes(state.Artist);
        var lyric = Encoding.UTF8.GetBytes(state.Lyric);
        var cover = state.Cover.Length == CoverLoader.Width * CoverLoader.Height * 4 ? state.Cover : [];
        using var buffer = new MemoryStream(28 + title.Length + artist.Length + lyric.Length + cover.Length);
        using var writer = new BinaryWriter(buffer, Encoding.UTF8, leaveOpen: true);
        writer.Write(Magic);
        writer.Write((ushort)1);
        writer.Write((ushort)1);
        writer.Write(state.IsRunning ? 1u : 0u);
        writer.Write((uint)title.Length);
        writer.Write((uint)artist.Length);
        writer.Write((uint)lyric.Length);
        writer.Write((uint)cover.Length);
        writer.Write(title);
        writer.Write(artist);
        writer.Write(lyric);
        writer.Write(cover);
        return buffer.ToArray();
    }
}

