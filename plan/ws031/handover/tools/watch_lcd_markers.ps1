# WS031: photograph the target LCD when the guest log reaches given markers.
# usage: powershell -ExecutionPolicy Bypass -File watch_lcd_markers.ps1 <outdir> <prefix> <timeout_s> "<marker>|<count>|<name>" ...
param([string]$OutDir, [string]$Prefix, [int]$TimeoutS, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Markers)
New-Item -ItemType Directory -Force $OutDir | Out-Null
$deadline = (Get-Date).AddSeconds($TimeoutS)
foreach ($m in $Markers) {
    $parts = $m.Split('|'); $pat = $parts[0]; $want = [int]$parts[1]; $name = $parts[2]
    while ((Get-Date) -lt $deadline) {
        $n = ssh agent-1 "ssh solaris10-man 'grep -acF -- \`"$pat\`" ~/bigbang/run-parity.log 2>/dev/null || true'"
        if ([int]("0" + ($n | Select-Object -Last 1)) -ge $want) { break }
        Start-Sleep -Milliseconds 700
    }
    if ((Get-Date) -ge $deadline) { Write-Output "timeout waiting for $name"; break }
    $f = Join-Path $OutDir ("{0}-{1}.jpg" -f $Prefix, $name)
    powershell -ExecutionPolicy Bypass -File C:\Work\qemu-work\tools\capture_lcd.ps1 $f | Out-Null
    Write-Output ("{0} {1} -> {2}" -f (Get-Date -Format HH:mm:ss), $name, $f)
}
