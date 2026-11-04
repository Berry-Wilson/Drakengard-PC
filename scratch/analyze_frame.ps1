Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile("$PSScriptRoot\..\scratch_frames\frame_000300.bmp")
Write-Host "Size: $($bmp.Width) x $($bmp.Height)"
$colors = @{}
for ($y = 0; $y -lt $bmp.Height; $y += 5) {
    for ($x = 0; $x -lt $bmp.Width; $x += 5) {
        $c = $bmp.GetPixel($x, $y)
        $key = "$($c.R),$($c.G),$($c.B)"
        if ($colors.ContainsKey($key)) {
            $colors[$key]++
        } else {
            $colors[$key] = 1
        }
    }
}
$bmp.Dispose()
$colors.GetEnumerator() | Sort-Object -Property Value -Descending | Select-Object -First 10 | ForEach-Object {
    Write-Host "$($_.Key) count: $($_.Value)"
}
