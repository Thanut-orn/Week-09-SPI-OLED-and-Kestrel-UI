using ESP32.Kestrel.Webserver.Services;

var builder = WebApplication.CreateBuilder(args);
builder.Services.AddSingleton<CalibrationService>();
builder.Services.AddSingleton<SerialTelemetryService>();
builder.Services.AddHostedService(serviceProvider =>
	serviceProvider.GetRequiredService<SerialTelemetryService>());
var app = builder.Build();

app.MapGet("/", () => "Hello World!");

app.MapGet("/api/telemetry", (CalibrationService cal, SerialTelemetryService telemetry) =>
{
	int raw = telemetry.LatestRaw;
	double calibrated = cal.Compute(raw);
	return Results.Ok(new
	{
		raw,
		calibrated = Math.Round(calibrated, 1),
		unit = cal.Settings.Unit,
		displayMsg = cal.CurrentOledMessage,
		lastReceivedUtc = telemetry.LastReceivedUtc,
		timestamp = DateTime.UtcNow
	});
});

app.MapPost("/api/potentiometer/calibrate", (CalibrationSettings newSettings, CalibrationService cal) =>
{
	try
	{
		cal.UpdateSettings(newSettings);
		cal.SetOledMessage("CALIBRATED OK");
		return Results.Ok(new { status = "success", settings = cal.Settings });
	}
	catch (ArgumentException ex)
	{
		return Results.BadRequest(new { status = "error", message = ex.Message });
	}
});

app.MapPost("/api/oled/message", (DisplayMessageRequest req, CalibrationService cal) =>
{
	if (string.IsNullOrWhiteSpace(req.Message))
	{
		return Results.BadRequest(new { status = "error", message = "ข้อความต้องไม่ว่างเปล่า" });
	}

	cal.SetOledMessage(req.Message);
	return Results.Ok(new { status = "success", current = cal.CurrentOledMessage });
});

app.Run();
