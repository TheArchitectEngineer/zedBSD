# WS031: take one photo with the host camera (pointed at the target machine's LCD) through WinRT MediaCapture.
# usage: powershell -ExecutionPolicy Bypass -File capture_lcd.ps1 <out.jpg>
param([Parameter(Mandatory = $true)][string]$OutFile)

Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Media.Capture.MediaCapture, Windows.Media.Capture, ContentType = WindowsRuntime]
$null = [Windows.Media.Capture.MediaCaptureInitializationSettings, Windows.Media.Capture, ContentType = WindowsRuntime]
$null = [Windows.Media.MediaProperties.ImageEncodingProperties, Windows.Media.MediaProperties, ContentType = WindowsRuntime]
$null = [Windows.Storage.StorageFolder, Windows.Storage, ContentType = WindowsRuntime]
$null = [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
$null = [Windows.Storage.CreationCollisionOption, Windows.Storage, ContentType = WindowsRuntime]

$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
$asTaskAction = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction' })[0]

function Await($op, [Type]$resultType) {
    $task = $asTaskGeneric.MakeGenericMethod($resultType).Invoke($null, @($op))
    if (-not $task.Wait(20000)) { throw "timeout" }
    $task.Result
}
function AwaitAction($op) {
    $task = $asTaskAction.Invoke($null, @($op))
    if (-not $task.Wait(20000)) { throw "timeout" }
}

$full = [System.IO.Path]::GetFullPath($OutFile)
$dir = [System.IO.Path]::GetDirectoryName($full)
$name = [System.IO.Path]::GetFileName($full)

$settings = New-Object Windows.Media.Capture.MediaCaptureInitializationSettings
$settings.StreamingCaptureMode = [Windows.Media.Capture.StreamingCaptureMode]::Video
$capture = New-Object Windows.Media.Capture.MediaCapture
AwaitAction ($capture.InitializeAsync($settings))
Start-Sleep -Milliseconds 1500   # let auto-exposure settle

$folder = Await ([Windows.Storage.StorageFolder]::GetFolderFromPathAsync($dir)) ([Windows.Storage.StorageFolder])
$file = Await ($folder.CreateFileAsync($name, [Windows.Storage.CreationCollisionOption]::ReplaceExisting)) ([Windows.Storage.StorageFile])
$props = [Windows.Media.MediaProperties.ImageEncodingProperties]::CreateJpeg()
AwaitAction ($capture.CapturePhotoToStorageFileAsync($props, $file))
$capture.Dispose()
Write-Output ("captured " + $full + " " + (Get-Item $full).Length + " bytes")
