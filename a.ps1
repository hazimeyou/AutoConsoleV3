$base = "http://127.0.0.1:5071"

Write-Host "=== sessions (initial) ==="
Invoke-RestMethod "$base/sessions"

Write-Host "=== start ping-test ==="
$start = Invoke-RestMethod -Method POST "$base/start" -ContentType "application/json" -Body '{"profile":"ping-test"}'
$sessionId = $start.data.sessionId
$sessionId

Start-Sleep -Seconds 1

Write-Host "=== sessions (after start) ==="
Invoke-RestMethod "$base/sessions"

Write-Host "=== logs ==="
Invoke-RestMethod "$base/logs?sessionId=$sessionId&limit=10"

Write-Host "=== plugin echo ==="
Invoke-RestMethod -Method POST "$base/plugin/execute" -ContentType "application/json" -Body '{"pluginId":"ext.echo","action":"echo","args":{"text":"hello"}}'

Write-Host "=== stop ==="
Invoke-RestMethod -Method POST "$base/stop" -ContentType "application/json" -Body ("{`"sessionId`":`"$sessionId`"}")

Write-Host "=== sessions (final) ==="
Invoke-RestMethod "$base/sessions"