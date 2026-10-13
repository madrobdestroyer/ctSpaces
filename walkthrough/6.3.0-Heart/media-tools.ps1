param([ValidateSet('Export','Inspect')][string]$Mode, [string]$Job)
$ErrorActionPreference = 'Stop'
$taskJob = Get-Content -LiteralPath $Job -Raw -Encoding UTF8 | ConvertFrom-Json
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$taskFileType = [Windows.Storage.StorageFile, Windows.Storage, ContentType=WindowsRuntime]
$taskFolderType = [Windows.Storage.StorageFolder, Windows.Storage, ContentType=WindowsRuntime]
$taskClipType = [Windows.Media.Editing.MediaClip, Windows.Media.Editing, ContentType=WindowsRuntime]
$taskTrackType = [Windows.Media.Editing.BackgroundAudioTrack, Windows.Media.Editing, ContentType=WindowsRuntime]
$taskCompositionType = [Windows.Media.Editing.MediaComposition, Windows.Media.Editing, ContentType=WindowsRuntime]
$taskOverlayType = [Windows.Media.Editing.MediaOverlay, Windows.Media.Editing, ContentType=WindowsRuntime]
$taskOverlayLayerType = [Windows.Media.Editing.MediaOverlayLayer, Windows.Media.Editing, ContentType=WindowsRuntime]
$taskEncodingType = [Windows.Media.MediaProperties.MediaEncodingProfile, Windows.Media.MediaProperties, ContentType=WindowsRuntime]
$taskReasonType = [Windows.Media.Transcoding.TranscodeFailureReason, Windows.Media.Transcoding, ContentType=WindowsRuntime]
$taskRectType = ($taskOverlayType.GetConstructors() | Where-Object { $_.GetParameters().Count -eq 3 } | Select-Object -First 1).GetParameters()[1].ParameterType
$taskQualityType = [Windows.Media.MediaProperties.VideoEncodingQuality, Windows.Media.MediaProperties, ContentType=WindowsRuntime]
$taskPreferenceType = [Windows.Media.Editing.MediaTrimmingPreference, Windows.Media.Editing, ContentType=WindowsRuntime]
$taskCollisionType = [Windows.Storage.CreationCollisionOption, Windows.Storage, ContentType=WindowsRuntime]
function Await-Operation($Operation, [Type]$ResultType, [Type]$ProgressType) {
  $taskArity = if ($ProgressType) { 2 } else { 1 }
  $taskMethod = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetParameters().Count -eq 1 -and
    $_.GetGenericArguments().Count -eq $taskArity -and $_.GetParameters()[0].ParameterType.Name -like 'IAsyncOperation*'
  } | Select-Object -First 1
  $taskGenericTypes = if ($ProgressType) { @($ResultType, $ProgressType) } else { @($ResultType) }
  $taskAsync = $taskMethod.MakeGenericMethod($taskGenericTypes).Invoke($null, @($Operation))
  return $taskAsync.GetAwaiter().GetResult()
}
function Local-File([string]$Path) {
  return Await-Operation ($taskFileType::GetFileFromPathAsync([IO.Path]::GetFullPath($Path))) $taskFileType
}
function Add-VectorItem($Vector, [Type]$ElementType, $Item) {
  $taskCollectionType = [System.Collections.Generic.ICollection``1].MakeGenericType(@($ElementType))
  $taskCollectionType.GetMethod('Add').Invoke($Vector, @($Item))
}
if ($Mode -eq 'Inspect') {
  $taskFile = Local-File $taskJob.output
  $taskProfile = Await-Operation ($taskEncodingType::CreateFromFileAsync($taskFile)) $taskEncodingType
  $taskVideo = $taskProfile.Video
  $taskAudio = $taskProfile.Audio
  $taskProperties = Await-Operation ($taskFile.Properties.GetVideoPropertiesAsync()) ([Windows.Storage.FileProperties.VideoProperties, Windows.Storage.FileProperties, ContentType=WindowsRuntime])
  $taskDecodeComposition = New-Object $taskCompositionType.FullName
  $taskDecodedClip = Await-Operation ($taskClipType::CreateFromFileAsync($taskFile)) $taskClipType
  Add-VectorItem $taskDecodeComposition.Clips $taskClipType $taskDecodedClip
  $taskDecoded = @()
  foreach ($taskThumbnail in $taskJob.thumbnails) {
    $taskThumbnailWidth = if ($taskThumbnail.width) { [int]$taskThumbnail.width } else { 960 }
    $taskThumbnailHeight = if ($taskThumbnail.height) { [int]$taskThumbnail.height } else { 540 }
    $taskStream = Await-Operation ($taskDecodeComposition.GetThumbnailAsync([TimeSpan]::FromSeconds([double]$taskThumbnail.time), $taskThumbnailWidth, $taskThumbnailHeight,
      [Windows.Media.Editing.VideoFramePrecision, Windows.Media.Editing, ContentType=WindowsRuntime]::NearestFrame)) ([Windows.Graphics.Imaging.ImageStream, Windows.Graphics.Imaging, ContentType=WindowsRuntime])
    $taskReader = [System.IO.WindowsRuntimeStreamExtensions]::AsStreamForRead($taskStream)
    $taskWriter = [IO.File]::Create([string]$taskThumbnail.path)
    try { $taskReader.CopyTo($taskWriter) } finally { $taskWriter.Dispose(); $taskReader.Dispose() }
    $taskDecoded += @{ time = $taskThumbnail.time; path = $taskThumbnail.path; bytes = (Get-Item -LiteralPath $taskThumbnail.path).Length }
  }
  $taskInfo = [ordered]@{ output = $taskJob.output; bytes = (Get-Item -LiteralPath $taskJob.output).Length;
    durationSeconds = $taskProperties.Duration.TotalSeconds; width = $taskVideo.Width; height = $taskVideo.Height;
    videoSubtype = $taskVideo.Subtype; videoBitrate = $taskVideo.Bitrate;
    fpsNumerator = $taskVideo.FrameRate.Numerator; fpsDenominator = $taskVideo.FrameRate.Denominator;
    audioSubtype = $taskAudio.Subtype; sampleRate = $taskAudio.SampleRate; channels = $taskAudio.ChannelCount;
    audioBitrate = $taskAudio.Bitrate; decodedFrames = $taskDecoded; offline = $true }
  [IO.File]::WriteAllText([string]$taskJob.inspection, ($taskInfo | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding($false)))
  $taskInfo | ConvertTo-Json -Depth 5
  exit 0
}
$taskComposition = New-Object $taskCompositionType.FullName
foreach ($taskPart in $taskJob.parts) {
  $taskInput = Local-File $taskPart.video
  $taskClip = Await-Operation ($taskClipType::CreateFromFileAsync($taskInput)) $taskClipType
  $taskClip.Volume = 0
  $taskClip.TrimTimeFromEnd = [TimeSpan]::FromSeconds([Math]::Max(0, $taskClip.OriginalDuration.TotalSeconds - [double]$taskPart.duration))
  Add-VectorItem $taskComposition.Clips $taskClipType $taskClip
}
if ($taskJob.videos.Count) {
  $taskLayer = New-Object $taskOverlayLayerType.FullName
  foreach ($taskVideoJob in $taskJob.videos) {
    $taskVideoFile = Local-File $taskVideoJob.path
    $taskClip = Await-Operation ($taskClipType::CreateFromFileAsync($taskVideoFile)) $taskClipType
    $taskClip.Volume = 0
    $taskClip.TrimTimeFromStart = [TimeSpan]::FromSeconds([double]$taskVideoJob.sourceStart)
    $taskClip.TrimTimeFromEnd = [TimeSpan]::FromSeconds([Math]::Max(0, $taskClip.OriginalDuration.TotalSeconds - [double]$taskVideoJob.sourceStart - [double]$taskVideoJob.duration))
    $taskRect = [Activator]::CreateInstance($taskRectType, [object[]]@([double]$taskVideoJob.x, [double]$taskVideoJob.y, [double]$taskVideoJob.width, [double]$taskVideoJob.height))
    $taskOverlay = [Activator]::CreateInstance($taskOverlayType, [object[]]@($taskClip, $taskRect, [double]1.0))
    $taskOverlay.Delay = [TimeSpan]::FromSeconds([double]$taskVideoJob.start)
    $taskOverlay.AudioEnabled = $false
    Add-VectorItem $taskLayer.Overlays $taskOverlayType $taskOverlay
  }
  Add-VectorItem $taskComposition.OverlayLayers $taskOverlayLayerType $taskLayer
}
$taskWav = Local-File $taskJob.audio
$taskTrack = Await-Operation ($taskTrackType::CreateFromFileAsync($taskWav)) $taskTrackType
$taskTrack.Volume = 1
Add-VectorItem $taskComposition.BackgroundAudioTracks $taskTrackType $taskTrack
$taskFolder = Await-Operation ($taskFolderType::GetFolderFromPathAsync([IO.Path]::GetDirectoryName([string]$taskJob.output))) $taskFolderType
$taskOutput = Await-Operation ($taskFolder.CreateFileAsync([IO.Path]::GetFileName([string]$taskJob.output), $taskCollisionType::ReplaceExisting)) $taskFileType
$taskProfile = $taskEncodingType::CreateMp4($taskQualityType::HD1080p)
$taskProfile.Video.Width = [uint32]$taskJob.width
$taskProfile.Video.Height = [uint32]$taskJob.height
$taskProfile.Video.Bitrate = [uint32]$taskJob.bitrate
$taskProfile.Video.FrameRate.Numerator = [uint32]$taskJob.fps
$taskProfile.Video.FrameRate.Denominator = 1
$taskProfile.Audio.SampleRate = 48000
$taskProfile.Audio.ChannelCount = 2
$taskProfile.Audio.Bitrate = 192000
Write-Output ('ENCODING ' + $taskComposition.Duration.TotalSeconds + ' seconds via Windows H.264/AAC')
$taskResult = Await-Operation ($taskComposition.RenderToFileAsync($taskOutput, $taskPreferenceType::Precise, $taskProfile)) $taskReasonType ([double])
if ($taskResult.ToString() -ne 'None') { throw ('Local MP4 export failed: ' + $taskResult) }
Write-Output ('EXPORTED ' + $taskJob.output)
