# AutoConsole v3

AutoConsole v3 is a generic Windows console execution platform.
It is not Minecraft-only and is designed to run general console applications.

## Repository structure

- `src/AutoConsole.Abstractions`
- `src/AutoConsole.Core`
- `src/AutoConsole.StandardPlugins`
- `src/AutoConsole.Cli`
- `profiles/examples` (example profile JSON files)
- `plugins/installed` (external plugin DLLs)

## Current implementation scope (v3.0.0)

- Process start/stop
- stdin write
- stdout/stderr line events
- Session model and state tracking
- Event dispatch and plugin host
- Standard plugin actions (minimal set currently implemented)
- External DLL plugin discovery/loading
- CLI client

## Build

```powershell
msbuild AutoConsoleV3.slnx /t:Build /p:Configuration=Debug /p:Platform=x64
```

Release:

```powershell
msbuild AutoConsoleV3.slnx /t:Build /p:Configuration=Release /p:Platform=x64
```

If your environment does not have `v145`, override toolset as needed:

```powershell
msbuild AutoConsoleV3.slnx /t:Build /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143
```

## Run

Debug output:

```powershell
.\build\Debug\AutoConsole.Cli.exe
```

Release output:

```powershell
.\x64\Release\AutoConsole.Cli.exe
```

## Profiles and `start` behavior

The CLI `start` command resolves profiles from:

- `profiles/examples/`

Rules:

- `start ping-test` -> `profiles/examples/ping-test.json`
- `start ping-test.json` -> `profiles/examples/ping-test.json`

Current example profile files in this repo:

- `ping-test.json`
- `python-script.json`
- `ffmpeg.json`

## Current session behavior

The CLI tracks the most recently started session as `current`.

- `current` prints current session ID or `none`
- These commands can omit `sessionId` and use current session:
  - `send`
  - `stop`
  - `plugin send_input`
  - `plugin wait_output`
  - `plugin stop_process`

## CLI commands (matches current `help`)

- `help`
- `ping`
- `start <profile>`
- `run <workflow>`
- `plugins`
- `current`
- `sessions`
- `stop [sessionId]`
- `send [sessionId] <text>`
- `plugin send_input [sessionId] <text>`
- `plugin info <pluginId>`
- `plugin wait_output [sessionId] <contains> <timeoutMs>`
- `plugin delay <durationMs>`
- `plugin timer <durationMs>`
- `plugin stop_process [sessionId]`
- `plugin emit_event <eventType> [sessionId] [payload]`
- `plugin call_plugin <pluginId> <action> [key=value ...]`
- `loglevel`
- `loglevel normal`
- `loglevel debug`
- `exit`

## Valid command examples (current parser)

```powershell
start ping-test
start ping-test.json
sessions
current
send list
stop
plugin wait_output "ping-3" 15000
plugin wait_output session-1 "ping-3" 15000
plugin delay 1000
plugin timer 1000
plugin stop_process
plugins
plugin info sample.echo
```

## External plugins

At startup, Core scans:

- `plugins/installed/*.dll`

Behavior:

- valid DLL plugins are loaded and registered
- invalid DLLs are skipped with error logging
- one bad DLL does not stop application startup

CLI inspection:

- `plugins`
- `plugin info <pluginId>`

## Versioning and tag release

Repository-level version defaults are defined in `Directory.Build.props`:

- `Version` defaults to `0.0.0` when not supplied
- `FileVersion` and `InformationalVersion` derive from `Version`

Tag release workflow:

- `.github/workflows/release-tag.yml`
- Trigger: push tag `v*` (example `v3.0.0`)
- Version mapping: `v3.0.0` -> `3.0.0`
- Builds Release x64 and injects `/p:Version=<tagVersion>`
- Creates packaged zip artifact
- Creates GitHub Release as draft + prerelease

## License

See `LICENSE.txt`.
