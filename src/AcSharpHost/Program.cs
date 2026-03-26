using System.Reflection;
using System.Text.Json;

var exeDir = AppContext.BaseDirectory;
var csPluginsDir = Path.GetFullPath(Path.Combine(exeDir, "csplugins"));

Console.Error.WriteLine($"[AcSharpHost] executable directory: {exeDir}");
Console.Error.WriteLine($"[AcSharpHost] resolved csplugins directory: {csPluginsDir}");

var pluginDllPaths = new List<string>();
if (Directory.Exists(csPluginsDir))
{
    pluginDllPaths.AddRange(Directory.EnumerateFiles(csPluginsDir, "*.dll", SearchOption.AllDirectories));
}

var loadedAssemblies = new List<Assembly>();
foreach (var pluginDllPath in pluginDllPaths)
{
    try
    {
        var assembly = Assembly.LoadFrom(pluginDllPath);
        loadedAssemblies.Add(assembly);
        Console.Error.WriteLine($"[AcSharpHost] loaded plugin assembly: {pluginDllPath}");
    }
    catch (Exception ex)
    {
        Console.Error.WriteLine($"[AcSharpHost] failed to load plugin assembly: {pluginDllPath} ({ex.Message})");
    }
}

Console.Error.WriteLine($"[AcSharpHost] plugin assemblies loaded: {loadedAssemblies.Count}");

while (true)
{
    var line = Console.ReadLine();
    if (line is null)
    {
        break;
    }

    if (string.IsNullOrWhiteSpace(line))
    {
        continue;
    }

    try
    {
        using var doc = JsonDocument.Parse(line);
        var root = doc.RootElement;
        var action = root.TryGetProperty("action", out var actionProp) ? actionProp.GetString() ?? string.Empty : string.Empty;

        var response = new
        {
            success = true,
            message = $"AcSharpHost executed action '{action}'",
            loadedPluginCount = loadedAssemblies.Count,
            csPluginsDirectory = csPluginsDir
        };

        Console.WriteLine(JsonSerializer.Serialize(response));
        Console.Out.Flush();
    }
    catch (Exception ex)
    {
        var errorResponse = new
        {
            success = false,
            message = ex.Message
        };

        Console.WriteLine(JsonSerializer.Serialize(errorResponse));
        Console.Out.Flush();
    }
}