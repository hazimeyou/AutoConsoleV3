using System.Collections;
using System.Globalization;
using System.Reflection;
using System.Threading;
using System.Text.Json;
using System.Text.Json.Nodes;

var exeDir = AppContext.BaseDirectory;
var csPluginsDir = Path.GetFullPath(Path.Combine(exeDir, "csplugins"));

Console.Error.WriteLine($"[AcSharpHost] executable directory: {exeDir}");
Console.Error.WriteLine($"[AcSharpHost] resolved csplugins directory: {csPluginsDir}");

var bridgeCallbacks = new BridgeCallbackClient();
var loadedPlugins = LoadPlugins(csPluginsDir, bridgeCallbacks);
Console.Error.WriteLine($"[AcSharpHost] plugin instances loaded: {loadedPlugins.Count}");

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

        var action = GetString(root, "action");
        if (string.IsNullOrWhiteSpace(action))
        {
            WriteError("missing action");
            continue;
        }

        switch (action)
        {
        case "list_plugins":
        {
            var payload = string.Join(
                "\n",
                loadedPlugins.Values
                    .OrderBy(p => p.Metadata.Id, StringComparer.OrdinalIgnoreCase)
                    .Select(p => BuildBridgeLine(p.Metadata)));
            WriteSuccess(new
            {
                bridgePayload = payload,
                pluginCount = loadedPlugins.Count
            });
            break;
        }
        case "get_plugin_info":
        {
            var pluginId = GetString(root, "pluginId");
            if (string.IsNullOrWhiteSpace(pluginId))
            {
                WriteError("missing pluginId");
                break;
            }

            if (!loadedPlugins.TryGetValue(pluginId, out var plugin))
            {
                WriteError($"plugin not found: {pluginId}");
                break;
            }

            WriteSuccess(new
            {
                bridgePayload = BuildBridgeLine(plugin.Metadata)
            });
            break;
        }
        case "execute_plugin":
        {
            var pluginId = GetString(root, "pluginId");
            var pluginAction = GetString(root, "pluginAction");
            if (string.IsNullOrWhiteSpace(pluginId) || string.IsNullOrWhiteSpace(pluginAction))
            {
                WriteError("pluginId and pluginAction are required");
                break;
            }

            if (!loadedPlugins.TryGetValue(pluginId, out var plugin))
            {
                WriteError($"plugin not found: {pluginId}");
                break;
            }

            var actionArgs = ReadArgs(root);
            NormalizeTransferArgs(pluginAction, actionArgs);
            plugin.Runtime.CurrentSessionId = ResolveCurrentSessionId(root, actionArgs);
            if (!EnsureInitialized(plugin, actionArgs, out var initError))
            {
                WriteError(initError ?? "plugin initialization failed");
                break;
            }
            if (!TryExecute(plugin, pluginAction, actionArgs, out var execError, out var execMessage))
            {
                WriteError(execError ?? $"plugin action failed: {pluginId}.{pluginAction}");
                break;
            }

            WriteSuccess(new
            {
                message = execMessage ?? string.Empty
            });
            break;
        }
        case "notify_stdout_line":
        {
            var sessionId = GetString(root, "sessionId") ?? string.Empty;
            var lineValue = GetString(root, "line") ?? string.Empty;
            foreach (var plugin in loadedPlugins.Values)
            {
                TryDispatchStdoutLine(plugin, sessionId, lineValue);
            }

            WriteSuccess(new { message = string.Empty });
            break;
        }
        case "echo":
        {
            var message = GetString(root, "args", "message") ?? string.Empty;
            WriteSuccess(new { message, echo = message });
            break;
        }
        default:
            WriteSuccess(new
            {
                message = $"AcSharpHost received action '{action}'"
            });
            break;
        }
    }
    catch (Exception ex)
    {
        WriteError(ex.Message);
    }
}

return;

static Dictionary<string, LoadedSharpPlugin> LoadPlugins(string csPluginsDir, BridgeCallbackClient bridgeCallbacks)
{
    var loadedPlugins = new Dictionary<string, LoadedSharpPlugin>(StringComparer.OrdinalIgnoreCase);
    if (!Directory.Exists(csPluginsDir))
    {
        Console.Error.WriteLine($"[AcSharpHost] csplugins directory not found: {csPluginsDir}");
        return loadedPlugins;
    }

    foreach (var pluginDllPath in Directory.EnumerateFiles(csPluginsDir, "*.dll", SearchOption.AllDirectories))
    {
        try
        {
            var assembly = Assembly.LoadFrom(pluginDllPath);
            Console.Error.WriteLine($"[AcSharpHost] loaded assembly: {pluginDllPath}");
            foreach (var type in SafeGetTypes(assembly))
            {
                if (type is null || !type.IsClass)
                {
                    continue;
                }
                if (type.IsAbstract && !type.IsSealed)
                {
                    continue;
                }

                if (!HasPluginShape(type))
                {
                    continue;
                }

                object? instance = null;
                var runtime = new PluginRuntimeState();
                if (!type.IsAbstract)
                {
                    if (!TryCreateInstance(type, runtime, bridgeCallbacks, out instance))
                    {
                        Console.Error.WriteLine($"[AcSharpHost] instance create failed: {type.FullName}");
                        Console.Error.WriteLine($"[AcSharpHost] constructors: {DescribeConstructors(type)}");
                    }
                }

                var metadata = ReadMetadata(instance, type, assembly.GetName().Name ?? "plugin");
                if (string.IsNullOrWhiteSpace(metadata.Id))
                {
                    Console.Error.WriteLine($"[AcSharpHost] plugin id missing: {type.FullName}");
                    continue;
                }

                if (loadedPlugins.ContainsKey(metadata.Id))
                {
                    Console.Error.WriteLine($"[AcSharpHost] duplicate plugin id skipped: {metadata.Id}");
                    Console.Error.WriteLine($"[AcSharpHost] execute candidates on skipped type: {DescribeExecuteCandidates(type)}");
                    continue;
                }

                loadedPlugins[metadata.Id] = new LoadedSharpPlugin(metadata, instance, type, runtime);
                Console.Error.WriteLine($"[AcSharpHost] loaded plugin: {metadata.Id} ({type.FullName})");
            }
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[AcSharpHost] failed to load assembly: {pluginDllPath} ({ex.Message})");
        }
    }

    return loadedPlugins;
}

static bool HasPluginShape(Type type)
{
    if (FindExecuteMethod(type) is not null)
    {
        return true;
    }

    return type.GetInterfaces().Any(i => i.Name.Contains("Plugin", StringComparison.OrdinalIgnoreCase));
}

static IEnumerable<Type?> SafeGetTypes(Assembly assembly)
{
    try
    {
        return assembly.GetTypes();
    }
    catch (ReflectionTypeLoadException ex)
    {
        return ex.Types;
    }
}

static string? GetString(JsonElement root, params string[] path)
{
    var current = root;
    foreach (var key in path)
    {
        if (current.ValueKind != JsonValueKind.Object || !current.TryGetProperty(key, out var next))
        {
            return null;
        }
        current = next;
    }

    return current.ValueKind switch
    {
        JsonValueKind.String => current.GetString(),
        JsonValueKind.Number => current.GetRawText(),
        JsonValueKind.True => "true",
        JsonValueKind.False => "false",
        _ => null
    };
}

static Dictionary<string, string> ReadArgs(JsonElement root)
{
    var args = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
    if (!root.TryGetProperty("args", out var argsElement) || argsElement.ValueKind != JsonValueKind.Object)
    {
        return args;
    }

    foreach (var prop in argsElement.EnumerateObject())
    {
        args[prop.Name] = prop.Value.ValueKind switch
        {
            JsonValueKind.String => prop.Value.GetString() ?? string.Empty,
            JsonValueKind.Number => prop.Value.GetRawText(),
            JsonValueKind.True => "true",
            JsonValueKind.False => "false",
            _ => prop.Value.GetRawText()
        };
    }

    return args;
}

static string ResolveCurrentSessionId(JsonElement root, Dictionary<string, string> args)
{
    var explicitCurrent = GetString(root, "currentSessionId");
    if (!string.IsNullOrWhiteSpace(explicitCurrent))
    {
        return explicitCurrent;
    }

    if (args.TryGetValue("sessionId", out var sessionId) && !string.IsNullOrWhiteSpace(sessionId))
    {
        return sessionId;
    }

    if (args.TryGetValue("fromSession", out var fromSession) && !string.IsNullOrWhiteSpace(fromSession))
    {
        return fromSession;
    }

    if (args.TryGetValue("toSession", out var toSession) && !string.IsNullOrWhiteSpace(toSession))
    {
        return toSession;
    }

    return string.Empty;
}

static void NormalizeTransferArgs(string action, Dictionary<string, string> args)
{
    if (!string.Equals(action, "transfer_item", StringComparison.OrdinalIgnoreCase))
    {
        return;
    }

    static string PickValue(Dictionary<string, string> map, params string[] keys)
    {
        foreach (var key in keys)
        {
            if (map.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value))
            {
                return value;
            }
        }

        return string.Empty;
    }

    var player = PickValue(args, "player");
    var fromPlayer = PickValue(args, "fromPlayer", "from_player", "sourcePlayer", "srcPlayer", "playerFrom");
    var toPlayer = PickValue(args, "toPlayer", "to_player", "targetPlayer", "dstPlayer", "playerTo");

    if (string.IsNullOrWhiteSpace(fromPlayer) && !string.IsNullOrWhiteSpace(player))
    {
        fromPlayer = player;
    }
    if (string.IsNullOrWhiteSpace(toPlayer) && !string.IsNullOrWhiteSpace(player))
    {
        toPlayer = player;
    }

    if (!string.IsNullOrWhiteSpace(fromPlayer))
    {
        args["fromPlayer"] = fromPlayer;
    }
    if (!string.IsNullOrWhiteSpace(toPlayer))
    {
        args["toPlayer"] = toPlayer;
    }
    if (string.IsNullOrWhiteSpace(player))
    {
        if (!string.IsNullOrWhiteSpace(fromPlayer))
        {
            args["player"] = fromPlayer;
        }
        else if (!string.IsNullOrWhiteSpace(toPlayer))
        {
            args["player"] = toPlayer;
        }
    }
}

static bool EnsureInitialized(LoadedSharpPlugin plugin, Dictionary<string, string> args, out string? error)
{
    error = null;
    if (plugin.Runtime.Initialized)
    {
        return true;
    }

    const BindingFlags flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.IgnoreCase;
    var initializeMethod = plugin.Type.GetMethod("Initialize", flags);
    if (initializeMethod is null)
    {
        plugin.Runtime.Initialized = true;
        return true;
    }

    var parameters = initializeMethod.GetParameters();
    if (parameters.Length != 1)
    {
        error = "unsupported Initialize signature";
        return false;
    }

    object initArg;
    try
    {
        initArg = ConvertArg(BuildInitConfig(args), parameters[0].ParameterType);
    }
    catch (Exception ex)
    {
        error = $"failed to build initialize argument: {ex.Message}";
        return false;
    }

    object? result;
    try
    {
        var target = initializeMethod.IsStatic ? null : plugin.Instance;
        if (!initializeMethod.IsStatic && target is null)
        {
            error = "plugin instance is not available for Initialize";
            return false;
        }

        result = initializeMethod.Invoke(target, new[] { initArg });
    }
    catch (TargetInvocationException ex)
    {
        error = ex.InnerException?.Message ?? ex.Message;
        return false;
    }
    catch (Exception ex)
    {
        error = ex.Message;
        return false;
    }

    if (result is null)
    {
        plugin.Runtime.Initialized = true;
        return true;
    }

    var resultType = result.GetType();
    var successProp = resultType.GetProperty("Success", flags);
    var errorProp = resultType.GetProperty("Error", flags);
    if (successProp is not null)
    {
        var success = successProp.GetValue(result) is bool b && b;
        if (!success)
        {
            error = errorProp?.GetValue(result)?.ToString() ?? "plugin initialize failed";
            return false;
        }
    }

    plugin.Runtime.Initialized = true;
    return true;
}

static Dictionary<string, object?> BuildInitConfig(Dictionary<string, string> args)
{
    static string First(Dictionary<string, string> map, params string[] keys)
    {
        foreach (var key in keys)
        {
            if (map.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value))
            {
                return value;
            }
        }

        return string.Empty;
    }

    var edition = First(args, "sekai.mc.edition", "edition", "fromEdition", "toEdition");
    var runtimeRoot = First(args, "sekai.mc.runtimeRoot", "runtimeRoot");
    var sessionId = First(args, "sessionId");
    var fromSession = First(args, "fromSession");
    var toSession = First(args, "toSession");
    var fromEdition = First(args, "fromEdition");
    var toEdition = First(args, "toEdition");

    var sessions = new Dictionary<string, object?>(StringComparer.OrdinalIgnoreCase);
    void AddSession(string id, string sessionEdition)
    {
        if (string.IsNullOrWhiteSpace(id) || string.IsNullOrWhiteSpace(sessionEdition))
        {
            return;
        }

        sessions[id] = new Dictionary<string, object?>(StringComparer.OrdinalIgnoreCase)
        {
            ["edition"] = sessionEdition
        };
    }

    AddSession(sessionId, edition);
    AddSession(fromSession, fromEdition);
    AddSession(toSession, toEdition);

    var mc = new Dictionary<string, object?>(StringComparer.OrdinalIgnoreCase);
    if (!string.IsNullOrWhiteSpace(edition))
    {
        mc["edition"] = edition;
    }
    if (!string.IsNullOrWhiteSpace(runtimeRoot))
    {
        mc["runtimeRoot"] = runtimeRoot;
    }
    if (sessions.Count > 0)
    {
        mc["sessions"] = sessions;
    }

    var sekai = new Dictionary<string, object?>(StringComparer.OrdinalIgnoreCase)
    {
        ["mc"] = mc
    };

    var config = new Dictionary<string, object?>(StringComparer.OrdinalIgnoreCase)
    {
        ["sekai"] = sekai
    };

    if (!string.IsNullOrWhiteSpace(edition))
    {
        config["sekai.mc.edition"] = edition;
    }
    if (!string.IsNullOrWhiteSpace(runtimeRoot))
    {
        config["sekai.mc.runtimeRoot"] = runtimeRoot;
    }
    if (sessions.Count > 0)
    {
        config["sekai.mc.sessions"] = sessions;
    }

    mc["bridgeMode"] = true;
    config["sekai.mc.bridgeMode"] = "true";

    return config;
}

static void TryDispatchStdoutLine(LoadedSharpPlugin plugin, string sessionId, string line)
{
    const BindingFlags flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.IgnoreCase;
    var method = plugin.Type.GetMethod("HandleStdoutLine", flags, null, new[] { typeof(string), typeof(string) }, null)
        ?? plugin.Type.GetMethod("HandleStdoutLine", flags, null, new[] { typeof(string) }, null);

    if (method is null)
    {
        return;
    }

    try
    {
        var target = method.IsStatic ? null : plugin.Instance;
        if (!method.IsStatic && target is null)
        {
            return;
        }

        var parameters = method.GetParameters();
        if (parameters.Length == 2)
        {
            method.Invoke(target, new object?[] { sessionId, line });
        }
        else if (parameters.Length == 1)
        {
            method.Invoke(target, new object?[] { line });
        }
    }
    catch
    {
        // stdout line dispatch should not crash host.
    }
}

static bool TryExecute(
    LoadedSharpPlugin plugin,
    string action,
    Dictionary<string, string> args,
    out string? error,
    out string? message)
{
    error = null;
    message = null;

    var preferStatic = plugin.Instance is null;
    var executeMethod = FindExecuteMethod(plugin.Type, preferStatic);
    if (executeMethod is null)
    {
        error = "Execute method not found";
        return false;
    }

    static bool TryInvoke(MethodInfo method, object? target, string action, Dictionary<string, string> args, out object? result, out string? invokeError)
    {
        invokeError = null;
        result = null;
        var parameters = method.GetParameters();
        try
        {
            if (parameters.Length == 2)
            {
                var param0 = ConvertArg(action, parameters[0].ParameterType);
                var param1 = ConvertArg(args, parameters[1].ParameterType);
                result = method.Invoke(target, new[] { param0, param1 });
            }
            else if (parameters.Length == 1)
            {
                var param0 = ConvertArg(action, parameters[0].ParameterType);
                result = method.Invoke(target, new[] { param0 });
            }
            else if (parameters.Length == 0)
            {
                result = method.Invoke(target, null);
            }
            else
            {
                invokeError = "unsupported Execute signature";
                return false;
            }

            return true;
        }
        catch (TargetInvocationException ex)
        {
            invokeError = ex.InnerException?.Message ?? ex.Message;
            return false;
        }
        catch (Exception ex)
        {
            invokeError = ex.Message;
            return false;
        }
    }

    object? result;
    object? target = executeMethod.IsStatic ? null : plugin.Instance;
    if (target is null && !executeMethod.IsStatic)
    {
        var staticFallback = FindExecuteMethod(plugin.Type, preferStatic: true, staticOnly: true);
        if (staticFallback is not null)
        {
            executeMethod = staticFallback;
            target = null;
        }
        else
        {
            error = "plugin instance is not available";
            return false;
        }
    }

    if (!TryInvoke(executeMethod, target, action, args, out result, out var invokeError))
    {
        error = invokeError;
        return false;
    }

    if (result is null)
    {
        return true;
    }

    if (result is bool b)
    {
        if (!b)
        {
            error = "plugin returned false";
        }
        return b;
    }

    if (result is string s)
    {
        message = s;
        return true;
    }

    var resultType = result.GetType();
    var successProp = resultType.GetProperty("Success", BindingFlags.Instance | BindingFlags.Public | BindingFlags.IgnoreCase);
    var messageProp = resultType.GetProperty("Message", BindingFlags.Instance | BindingFlags.Public | BindingFlags.IgnoreCase);
    var errorProp = resultType.GetProperty("Error", BindingFlags.Instance | BindingFlags.Public | BindingFlags.IgnoreCase);
    var reasonProp = resultType.GetProperty("Reason", BindingFlags.Instance | BindingFlags.Public | BindingFlags.IgnoreCase);
    var detailProp = resultType.GetProperty("Detail", BindingFlags.Instance | BindingFlags.Public | BindingFlags.IgnoreCase);
    if (successProp is not null)
    {
        var successValue = successProp.GetValue(result);
        var success = successValue is bool sb && sb;
        var msg = messageProp?.GetValue(result)?.ToString();
        if (success)
        {
            message = msg;
            return true;
        }

        var err = errorProp?.GetValue(result)?.ToString();
        var reason = reasonProp?.GetValue(result)?.ToString();
        var detail = detailProp?.GetValue(result)?.ToString();
        error = FirstNonEmpty(msg, err, reason, detail) ?? "plugin result reported failure";
        return false;
    }

    message = result.ToString();
    return true;
}

static object ConvertArg(object value, Type targetType)
{
    if (targetType.IsInstanceOfType(value))
    {
        return value;
    }

    if (targetType == typeof(string))
    {
        return value.ToString() ?? string.Empty;
    }

    if (typeof(IDictionary<string, string>).IsAssignableFrom(targetType))
    {
        return value;
    }

    if (targetType.IsGenericType)
    {
        var genericDef = targetType.GetGenericTypeDefinition();
        var genericArgs = targetType.GetGenericArguments();
        var isReadOnlyMap = genericDef == typeof(IReadOnlyDictionary<,>);
        var isMutableMap = genericDef == typeof(Dictionary<,>);
        if ((isReadOnlyMap || isMutableMap) && genericArgs[0] == typeof(string))
        {
            var src = (Dictionary<string, string>)value;
            if (genericArgs[1] == typeof(object))
            {
                return src.ToDictionary(pair => pair.Key, pair => (object)pair.Value, StringComparer.OrdinalIgnoreCase);
            }

            if (genericArgs[1] == typeof(string))
            {
                return src;
            }
        }
    }

    if (targetType.FullName == "System.Collections.Generic.Dictionary`2[System.String,System.Text.Json.JsonElement]")
    {
        var src = (Dictionary<string, string>)value;
        var jsonMap = new Dictionary<string, JsonElement>(StringComparer.OrdinalIgnoreCase);
        foreach (var pair in src)
        {
            var node = JsonValue.Create(pair.Value) ?? JsonValue.Create(string.Empty)!;
            jsonMap[pair.Key] = JsonSerializer.SerializeToElement(node);
        }
        return jsonMap;
    }

    if (typeof(IDictionary).IsAssignableFrom(targetType))
    {
        return value;
    }

    return value;
}

static SharpPluginMetadata ReadMetadata(object? instance, Type type, string assemblyName)
{
    object source = instance ?? type;
    const BindingFlags flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.IgnoreCase;
    var metadataProperty = type.GetProperty("Metadata", flags);
    if (metadataProperty is not null)
    {
        var metadataValue = metadataProperty.GetValue(metadataProperty.GetMethod?.IsStatic == true ? null : instance);
        if (metadataValue is not null)
        {
            source = metadataValue;
        }
    }

    else
    {
        var metadataMethod = type.GetMethod("Metadata", flags);
        if (metadataMethod is not null && metadataMethod.GetParameters().Length == 0)
        {
            var metadataValue = metadataMethod.Invoke(metadataMethod.IsStatic ? null : instance, null);
            if (metadataValue is not null)
            {
                source = metadataValue;
            }
        }
    }

    var id = ReadStringProperty(source, "Id", "id") ?? string.Empty;
    if (string.IsNullOrWhiteSpace(id))
    {
        id = NormalizePluginId(assemblyName);
    }
    var name = ReadStringProperty(source, "Name", "name") ?? id;
    var displayName = ReadStringProperty(source, "DisplayName", "displayName") ?? name;
    var version = ReadStringProperty(source, "Version", "version") ?? "unknown";
    var apiVersion = ReadStringProperty(source, "ApiVersion", "apiVersion") ?? "1.0.0";
    var author = ReadStringProperty(source, "Author", "author") ?? "unknown";
    var description = ReadStringProperty(source, "Description", "description") ?? string.Empty;
    var capabilities = ReadStringArrayProperty(source, "Capabilities", "capabilities");

    return new SharpPluginMetadata(
        Id: id,
        Name: string.IsNullOrWhiteSpace(name) ? id : name,
        DisplayName: string.IsNullOrWhiteSpace(displayName) ? (string.IsNullOrWhiteSpace(name) ? id : name) : displayName,
        Version: version,
        ApiVersion: apiVersion,
        Author: author,
        Description: description,
        Capabilities: capabilities);
}

static MethodInfo? FindExecuteMethod(Type type, bool preferStatic = false, bool staticOnly = false)
{
    static int Priority(string name)
    {
        if (string.Equals(name, "Execute", StringComparison.OrdinalIgnoreCase)) return 0;
        if (string.Equals(name, "ExecuteAction", StringComparison.OrdinalIgnoreCase)) return 1;
        if (string.Equals(name, "HandleAction", StringComparison.OrdinalIgnoreCase)) return 2;
        if (name.Contains("Execute", StringComparison.OrdinalIgnoreCase)) return 3;
        return 10;
    }

    var methods = type.GetMethods(BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public)
        .Where(m => Priority(m.Name) < 10)
        .Where(m => !staticOnly || m.IsStatic);

    if (preferStatic)
    {
        methods = methods
            .OrderByDescending(m => m.IsStatic)
            .ThenBy(m => Priority(m.Name))
            .ThenBy(m => m.GetParameters().Length);
    }
    else
    {
        methods = methods
            .OrderBy(m => Priority(m.Name))
            .ThenBy(m => m.GetParameters().Length)
            .ThenByDescending(m => m.IsStatic);
    }

    return methods.FirstOrDefault();
}

static string DescribeConstructors(Type type)
{
    const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
    var ctors = type.GetConstructors(flags);
    if (ctors.Length == 0)
    {
        return "(none)";
    }

    return string.Join(
        "; ",
        ctors.Select(c =>
        {
            var p = string.Join(", ", c.GetParameters().Select(x => $"{x.ParameterType.Name} {x.Name}"));
            return $"({p})";
        }));
}

static string DescribeExecuteCandidates(Type type)
{
    const BindingFlags flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public;
    var methods = type.GetMethods(flags)
        .Where(m => m.Name.Contains("Execute", StringComparison.OrdinalIgnoreCase)
            || m.Name.Contains("Handle", StringComparison.OrdinalIgnoreCase))
        .ToArray();
    if (methods.Length == 0)
    {
        return "(none)";
    }

    return string.Join(
        "; ",
        methods.Select(m =>
        {
            var p = string.Join(", ", m.GetParameters().Select(x => $"{x.ParameterType.Name} {x.Name}"));
            var kind = m.IsStatic ? "static" : "instance";
            return $"{kind} {m.Name}({p})";
        }));
}

static bool TryCreateInstance(
    Type type,
    PluginRuntimeState runtime,
    BridgeCallbackClient bridgeCallbacks,
    out object? instance)
{
    instance = null;
    try
    {
        instance = Activator.CreateInstance(type, nonPublic: true);
        if (instance is not null)
        {
            return true;
        }
    }
    catch
    {
        // Fall through to constructor-based injection.
    }

    const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
    foreach (var ctor in type.GetConstructors(flags).OrderBy(c => c.GetParameters().Length))
    {
        if (!TryBuildConstructorArgs(ctor, runtime, bridgeCallbacks, out var args))
        {
            Console.Error.WriteLine($"[AcSharpHost] ctor arg build failed: {type.FullName}::{ctor}");
            continue;
        }

        try
        {
            instance = ctor.Invoke(args);
            if (instance is not null)
            {
                return true;
            }
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[AcSharpHost] ctor invoke failed: {type.FullName}::{ctor} ({ex.Message})");
        }
    }

    return false;
}

static bool TryBuildConstructorArgs(
    ConstructorInfo ctor,
    PluginRuntimeState runtime,
    BridgeCallbackClient bridgeCallbacks,
    out object?[] args)
{
    var parameters = ctor.GetParameters();
    args = new object?[parameters.Length];
    for (var i = 0; i < parameters.Length; i++)
    {
        if (!TryBuildArgument(parameters[i], runtime, bridgeCallbacks, out args[i]))
        {
            Console.Error.WriteLine($"[AcSharpHost] unsupported ctor arg: {parameters[i].ParameterType.FullName} {parameters[i].Name}");
            return false;
        }
    }

    return true;
}

static bool TryBuildArgument(
    ParameterInfo parameter,
    PluginRuntimeState runtime,
    BridgeCallbackClient bridgeCallbacks,
    out object? value)
{
    value = null;
    var t = parameter.ParameterType;
    var name = parameter.Name ?? string.Empty;

    if (t == typeof(Func<bool>))
    {
        if (name.Contains("hasCurrentSession", StringComparison.OrdinalIgnoreCase))
        {
            value = (Func<bool>)(() => bridgeCallbacks.CallBool(
                "has_current_session",
                runtime,
                new Dictionary<string, string>(),
                out var v,
                out _)
                && v);
            return true;
        }

        if (name.Contains("isCurrentSessionRunning", StringComparison.OrdinalIgnoreCase))
        {
            value = (Func<bool>)(() => bridgeCallbacks.CallBool(
                "is_current_session_running",
                runtime,
                new Dictionary<string, string>(),
                out var v,
                out _)
                && v);
            return true;
        }

        if (name.Contains("restart", StringComparison.OrdinalIgnoreCase))
        {
            value = (Func<bool>)(() => bridgeCallbacks.CallBool(
                "restart_current_session",
                runtime,
                new Dictionary<string, string>(),
                out var v,
                out _)
                && v);
            return true;
        }

        value = (Func<bool>)(() => false);
        return true;
    }

    if (t == typeof(Func<string, string>))
    {
        value = (Func<string, string>)((string text) =>
        {
            if (bridgeCallbacks.CallString(
                "send_input_current_session",
                runtime,
                new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
                {
                    ["text"] = text ?? string.Empty
                },
                out var response,
                out var error))
            {
                return response;
            }

            return error ?? "send_input_current_session failed";
        });
        return true;
    }

    if (t == typeof(Action<string>))
    {
        value = (Action<string>)((string message) =>
        {
            bridgeCallbacks.CallString(
                "emit_event",
                runtime,
                new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
                {
                    ["level"] = "info",
                    ["message"] = message ?? string.Empty
                },
                out _,
                out _);
        });
        return true;
    }

    if (t == typeof(Func<string, bool>))
    {
        if (name.Contains("exists", StringComparison.OrdinalIgnoreCase))
        {
            value = (Func<string, bool>)((string sessionId) =>
                bridgeCallbacks.CallBool(
                    "session_exists",
                    runtime,
                    new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
                    {
                        ["sessionId"] = sessionId ?? string.Empty
                    },
                    out var v,
                    out _)
                && v);
            return true;
        }

        value = (Func<string, bool>)((string sessionId) =>
            bridgeCallbacks.CallBool(
                "session_running",
                runtime,
                new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
                {
                    ["sessionId"] = sessionId ?? string.Empty
                },
                out var v,
                out _)
            && v);
        return true;
    }

    if (t == typeof(Func<string, string, string>))
    {
        value = (Func<string, string, string>)((string sessionId, string text) =>
        {
            if (bridgeCallbacks.CallString(
                "send_input_to_session",
                runtime,
                new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
                {
                    ["sessionId"] = sessionId ?? string.Empty,
                    ["text"] = text ?? string.Empty
                },
                out var response,
                out var error))
            {
                return response;
            }

            return error ?? "send_input_to_session failed";
        });
        return true;
    }

    if (t.IsGenericType && t.GetGenericTypeDefinition() == typeof(Func<>))
    {
        var resultType = t.GetGenericArguments()[0];
        if (IsCommandSendResultType(resultType))
        {
            var factory = typeof(BridgeDelegateFactory)
                .GetMethod(nameof(BridgeDelegateFactory.CreateRestartCurrentSessionFunc), BindingFlags.Static | BindingFlags.Public)?
                .MakeGenericMethod(resultType);
            value = factory?.Invoke(null, new object?[] { bridgeCallbacks, runtime });
            return value is not null;
        }
    }

    if (t.IsGenericType && t.GetGenericTypeDefinition() == typeof(Func<,>))
    {
        var genericArgs = t.GetGenericArguments();
        if (genericArgs[0] == typeof(string) && IsCommandSendResultType(genericArgs[1]))
        {
            var factory = typeof(BridgeDelegateFactory)
                .GetMethod(nameof(BridgeDelegateFactory.CreateSendInputCurrentSessionFunc), BindingFlags.Static | BindingFlags.Public)?
                .MakeGenericMethod(genericArgs[1]);
            value = factory?.Invoke(null, new object?[] { bridgeCallbacks, runtime });
            return value is not null;
        }
    }

    if (t.IsGenericType && t.GetGenericTypeDefinition() == typeof(Func<,,>))
    {
        var genericArgs = t.GetGenericArguments();
        if (genericArgs[0] == typeof(string)
            && genericArgs[1] == typeof(string)
            && IsCommandSendResultType(genericArgs[2]))
        {
            var factory = typeof(BridgeDelegateFactory)
                .GetMethod(nameof(BridgeDelegateFactory.CreateSendInputToSessionFunc), BindingFlags.Static | BindingFlags.Public)?
                .MakeGenericMethod(genericArgs[2]);
            value = factory?.Invoke(null, new object?[] { bridgeCallbacks, runtime });
            return value is not null;
        }
    }

    if (t.IsGenericType && t.GetGenericTypeDefinition() == typeof(Action<>))
    {
        var argType = t.GetGenericArguments()[0];
        if (string.Equals(argType.Name, "McEvent", StringComparison.Ordinal))
        {
            var factory = typeof(BridgeDelegateFactory)
                .GetMethod(nameof(BridgeDelegateFactory.CreateNoOpAction), BindingFlags.Static | BindingFlags.Public)?
                .MakeGenericMethod(argType);
            value = factory?.Invoke(null, null);
            return value is not null;
        }
    }

    if (!t.IsValueType)
    {
        value = null;
        return true;
    }

    if (Nullable.GetUnderlyingType(t) is not null)
    {
        value = null;
        return true;
    }

    try
    {
        value = Activator.CreateInstance(t);
        return true;
    }
    catch
    {
        return false;
    }
}

static bool IsCommandSendResultType(Type type)
{
    return string.Equals(type.Name, "CommandSendResult", StringComparison.Ordinal);
}

static string NormalizePluginId(string assemblyName)
{
    if (string.IsNullOrWhiteSpace(assemblyName))
    {
        return "csharp.plugin";
    }

    var tokens = new List<string>();
    var current = new List<char>();
    foreach (var ch in assemblyName)
    {
        if (!char.IsLetterOrDigit(ch))
        {
            if (current.Count > 0)
            {
                tokens.Add(new string(current.ToArray()));
                current.Clear();
            }
            continue;
        }

        if (char.IsUpper(ch) && current.Count > 0 && !char.IsUpper(current[^1]))
        {
            tokens.Add(new string(current.ToArray()));
            current.Clear();
        }
        current.Add(ch);
    }
    if (current.Count > 0)
    {
        tokens.Add(new string(current.ToArray()));
    }

    if (tokens.Count > 0 && string.Equals(tokens[^1], "Plugin", StringComparison.OrdinalIgnoreCase))
    {
        tokens.RemoveAt(tokens.Count - 1);
    }
    tokens.RemoveAll(t => string.Equals(t, "Sharp", StringComparison.OrdinalIgnoreCase));
    for (int i = 0; i < tokens.Count; ++i)
    {
        if (tokens[i].Length > 5 && tokens[i].EndsWith("Sharp", StringComparison.OrdinalIgnoreCase))
        {
            tokens[i] = tokens[i][..^5];
        }
    }

    var merged = new List<string>();
    var singleBuffer = string.Empty;
    foreach (var token in tokens)
    {
        if (token.Length == 1)
        {
            singleBuffer += token.ToLowerInvariant();
            continue;
        }

        if (!string.IsNullOrEmpty(singleBuffer))
        {
            merged.Add(singleBuffer);
            singleBuffer = string.Empty;
        }
        merged.Add(token.ToLowerInvariant());
    }
    if (!string.IsNullOrEmpty(singleBuffer))
    {
        merged.Add(singleBuffer);
    }

    if (merged.Count == 0)
    {
        return "csharp.plugin";
    }

    return string.Join(".", merged);
}

static string? ReadStringProperty(object source, params string[] names)
{
    var type = source.GetType();
    const BindingFlags flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.IgnoreCase;
    foreach (var name in names)
    {
        var prop = type.GetProperty(name, flags);
        if (prop is null)
        {
            continue;
        }

        var target = prop.GetMethod?.IsStatic == true ? null : source;
        var value = prop.GetValue(target)?.ToString();
        if (!string.IsNullOrWhiteSpace(value))
        {
            return value;
        }
    }

    return null;
}

static string? FirstNonEmpty(params string?[] values)
{
    foreach (var value in values)
    {
        if (!string.IsNullOrWhiteSpace(value))
        {
            return value;
        }
    }

    return null;
}

static List<string> ReadStringArrayProperty(object source, params string[] names)
{
    var type = source.GetType();
    const BindingFlags flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.IgnoreCase;
    foreach (var name in names)
    {
        var prop = type.GetProperty(name, flags);
        if (prop is null)
        {
            continue;
        }

        var target = prop.GetMethod?.IsStatic == true ? null : source;
        var value = prop.GetValue(target);
        if (value is IEnumerable enumerable)
        {
            var list = new List<string>();
            foreach (var item in enumerable)
            {
                if (item is null)
                {
                    continue;
                }
                var text = item.ToString();
                if (!string.IsNullOrWhiteSpace(text))
                {
                    list.Add(text);
                }
            }
            return list;
        }
    }

    return new List<string>();
}

static string BuildBridgeLine(SharpPluginMetadata metadata)
{
    static string Sanitize(string text)
    {
        return text.Replace("\t", " ").Replace("\r", " ").Replace("\n", " ");
    }

    var caps = string.Join(",", metadata.Capabilities.Select(Sanitize));
    return string.Join(
        "\t",
        Sanitize(metadata.Id),
        Sanitize(metadata.Name),
        Sanitize(metadata.DisplayName),
        Sanitize(metadata.Version),
        Sanitize(metadata.ApiVersion),
        Sanitize(metadata.Author),
        Sanitize(metadata.Description),
        caps);
}

static void WriteSuccess(object payload)
{
    var root = new JsonObject
    {
        ["success"] = true
    };

    var payloadNode = JsonSerializer.SerializeToNode(payload);
    if (payloadNode is JsonObject payloadObject)
    {
        foreach (var pair in payloadObject)
        {
            root[pair.Key] = pair.Value?.DeepClone();
        }
    }

    Console.WriteLine(root.ToJsonString());
    Console.Out.Flush();
}

static void WriteError(string message)
{
    var payload = new JsonObject
    {
        ["success"] = false,
        ["message"] = message
    };

    Console.WriteLine(payload.ToJsonString());
    Console.Out.Flush();
}

internal sealed record SharpPluginMetadata(
    string Id,
    string Name,
    string DisplayName,
    string Version,
    string ApiVersion,
    string Author,
    string Description,
    List<string> Capabilities);

internal sealed record LoadedSharpPlugin(
    SharpPluginMetadata Metadata,
    object? Instance,
    Type Type,
    PluginRuntimeState Runtime);

internal sealed class PluginRuntimeState
{
    public string CurrentSessionId { get; set; } = string.Empty;
    public bool Initialized { get; set; }
}

internal static class BridgeDelegateFactory
{
    public static Func<TResult> CreateRestartCurrentSessionFunc<TResult>(BridgeCallbackClient bridgeCallbacks, PluginRuntimeState runtime)
    {
        return () =>
        {
            if (bridgeCallbacks.CallString(
                "restart_current_session",
                runtime,
                new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase),
                out _,
                out var error))
            {
                return CreateCommandSendResult<TResult>(true, null);
            }

            return CreateCommandSendResult<TResult>(false, error ?? "restart_current_session failed");
        };
    }

    public static Func<string, TResult> CreateSendInputCurrentSessionFunc<TResult>(BridgeCallbackClient bridgeCallbacks, PluginRuntimeState runtime)
    {
        return (string text) =>
        {
            if (bridgeCallbacks.CallString(
                "send_input_current_session",
                runtime,
                new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
                {
                    ["text"] = text ?? string.Empty
                },
                out _,
                out var error))
            {
                return CreateCommandSendResult<TResult>(true, null);
            }

            return CreateCommandSendResult<TResult>(false, error ?? "send_input_current_session failed");
        };
    }

    public static Func<string, string, TResult> CreateSendInputToSessionFunc<TResult>(BridgeCallbackClient bridgeCallbacks, PluginRuntimeState runtime)
    {
        return (string sessionId, string text) =>
        {
            if (bridgeCallbacks.CallString(
                "send_input_to_session",
                runtime,
                new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
                {
                    ["sessionId"] = sessionId ?? string.Empty,
                    ["text"] = text ?? string.Empty
                },
                out _,
                out var error))
            {
                return CreateCommandSendResult<TResult>(true, null);
            }

            return CreateCommandSendResult<TResult>(false, error ?? "send_input_to_session failed");
        };
    }

    public static Action<T> CreateNoOpAction<T>()
    {
        return _ => { };
    }

    private static TResult CreateCommandSendResult<TResult>(bool success, string? error)
    {
        var type = typeof(TResult);
        object? value = null;

        if (success)
        {
            var ok = type.GetMethod("Ok", BindingFlags.Public | BindingFlags.Static, null, Type.EmptyTypes, null);
            if (ok is not null)
            {
                value = ok.Invoke(null, null);
            }
        }
        else
        {
            var fail = type.GetMethod("Fail", BindingFlags.Public | BindingFlags.Static, null, new[] { typeof(string) }, null);
            if (fail is not null)
            {
                value = fail.Invoke(null, new object?[] { error ?? "failed" });
            }
        }

        value ??= Activator.CreateInstance(type);
        if (value is not null)
        {
            var successProp = type.GetProperty("Success", BindingFlags.Public | BindingFlags.Instance | BindingFlags.IgnoreCase);
            if (successProp?.CanWrite == true)
            {
                successProp.SetValue(value, success);
            }

            var errorProp = type.GetProperty("Error", BindingFlags.Public | BindingFlags.Instance | BindingFlags.IgnoreCase);
            if (errorProp?.CanWrite == true && !success)
            {
                errorProp.SetValue(value, error ?? "failed");
            }
        }

        return value is TResult typed ? typed : default!;
    }
}

internal sealed class BridgeCallbackClient
{
    private long requestCounter = 0;

    public bool CallBool(
        string callbackAction,
        PluginRuntimeState runtime,
        Dictionary<string, string> args,
        out bool value,
        out string? error)
    {
        value = false;
        if (!CallString(callbackAction, runtime, args, out var rawValue, out error))
        {
            return false;
        }

        value = string.Equals(rawValue, "true", StringComparison.OrdinalIgnoreCase);
        return true;
    }

    public bool CallString(
        string callbackAction,
        PluginRuntimeState runtime,
        Dictionary<string, string> args,
        out string value,
        out string? error)
    {
        value = string.Empty;
        error = null;

        var requestId = Interlocked.Increment(ref requestCounter).ToString(CultureInfo.InvariantCulture);
        var payload = new JsonObject
        {
            ["bridgeRequest"] = "callback",
            ["requestId"] = requestId,
            ["callbackAction"] = callbackAction
        };

        if (!string.IsNullOrWhiteSpace(runtime.CurrentSessionId))
        {
            payload["arg_currentSessionId"] = runtime.CurrentSessionId;
        }

        foreach (var pair in args)
        {
            payload["arg_" + pair.Key] = pair.Value ?? string.Empty;
        }

        Console.WriteLine(payload.ToJsonString());
        Console.Out.Flush();

        while (true)
        {
            var responseLine = Console.ReadLine();
            if (responseLine is null)
            {
                error = "bridge callback response is null";
                return false;
            }

            using var responseDoc = JsonDocument.Parse(responseLine);
            var root = responseDoc.RootElement;
            var responseType = ExtractString(root, "bridgeResponse");
            if (!string.Equals(responseType, "callback", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var responseId = ExtractString(root, "requestId");
            if (!string.Equals(responseId, requestId, StringComparison.Ordinal))
            {
                continue;
            }

            var success = root.TryGetProperty("success", out var successElement)
                && successElement.ValueKind == JsonValueKind.True;

            if (success)
            {
                value = ExtractString(root, "value") ?? string.Empty;
                return true;
            }

            error = ExtractString(root, "message") ?? "bridge callback failed";
            return false;
        }
    }

    private static string? ExtractString(JsonElement root, string key)
    {
        if (!root.TryGetProperty(key, out var value))
        {
            return null;
        }

        return value.ValueKind switch
        {
            JsonValueKind.String => value.GetString(),
            JsonValueKind.Number => value.GetRawText(),
            JsonValueKind.True => "true",
            JsonValueKind.False => "false",
            _ => null
        };
    }
}
