Add-Type -AssemblyName System.Drawing
$names = @("frame_000255", "frame_000260", "frame_000270", "frame_000275", "frame_000280")
foreach ($name in $names) {
    $bmpPath = "$PSScriptRoot\scratch_frames\$name.bmp"
    $pngPath = "$PSScriptRoot\scratch_frames\$name.png"
    if (Test-Path $bmpPath) {
        $bmp = [System.Drawing.Bitmap]::FromFile($bmpPath)
        $bmp.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        Write-Host "Converted $name to PNG at $pngPath"
    }
}
