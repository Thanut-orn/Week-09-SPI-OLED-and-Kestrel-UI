using System.Globalization;
using System.IO.Ports;

namespace ESP32.Kestrel.Webserver.Services;

public sealed class SerialTelemetryService : BackgroundService
{
    private readonly CalibrationService _calibration;
    private readonly object _sync = new();
    private readonly string _portName;
    private int _latestRaw;
    private DateTime _lastReceivedUtc = DateTime.MinValue;
    private SerialPort? _serialPort;

    public SerialTelemetryService(CalibrationService calibration)
    {
        _calibration = calibration;
        _portName = Environment.GetEnvironmentVariable("ESP32_PORT") ?? "COM12";
    }

    public int LatestRaw
    {
        get
        {
            lock (_sync)
            {
                return _latestRaw;
            }
        }
    }

    public DateTime LastReceivedUtc
    {
        get
        {
            lock (_sync)
            {
                return _lastReceivedUtc;
            }
        }
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        try
        {
            _serialPort = new SerialPort(_portName, 115200)
            {
                NewLine = "\n",
                ReadTimeout = 500,
                WriteTimeout = 500
            };
            _serialPort.Open();

            while (!stoppingToken.IsCancellationRequested)
            {
                try
                {
                    string line = _serialPort.ReadLine().Trim();
                    if (line.StartsWith("ADC:", StringComparison.OrdinalIgnoreCase)
                        && int.TryParse(line[4..], NumberStyles.Integer, CultureInfo.InvariantCulture, out int raw))
                    {
                        lock (_sync)
                        {
                            _latestRaw = raw;
                            _lastReceivedUtc = DateTime.UtcNow;
                        }

                        int percent = (int)Math.Round(_calibration.Compute(raw));
                        string message = _calibration.CurrentOledMessage;
                        _serialPort.WriteLine($"SET:{percent}:{message}");
                    }
                }
                catch (TimeoutException)
                {
                    await Task.Yield();
                }
            }
        }
        catch (Exception ex) when (ex is UnauthorizedAccessException or IOException or InvalidOperationException)
        {
            Console.Error.WriteLine($"Serial telemetry unavailable on {_portName}: {ex.Message}");
        }
    }

    public override void Dispose()
    {
        _serialPort?.Dispose();
        base.Dispose();
    }
}