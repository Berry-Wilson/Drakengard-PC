Add-Type -AssemblyName System.Drawing
$dir = "$PSScriptRoot\..\scratch_frames"
$files = Get-ChildItem -Path $dir -Filter "frame_000*.bmp" | Select-Object -Last 15
foreach ($f in $files) {
    $bmp = [System.Drawing.Bitmap]::FromFile($f.FullName)
    $nonGrey = 0
    $nonBlack = 0
    $maxR = 0; $maxG = 0; $maxB = 0
    for ($y = 0; $y -lt $bmp.Height; $y += 4) {
        for ($x = 0; $x -lt $bmp.Width; $x += 4) {
            $c = $bmp.GetPixel($x, $y)
            if ($c.R -gt $maxR) { $maxR = $c.R }
            if ($c.G -gt $maxG) { $maxG = $c.G }
            if ($c.B -gt $maxB) { $maxB = $c.B }
            if ($c.R -ne 0 -or $c.G -ne 0 -or $c.B -ne 0) { $nonBlack++ }
            if ($c.R -ne 128 -or $c.G -ne 128 -or $c.B -ne 128) {
                if ($c.R -ne 0 -or $c.G -ne 0 -or $c.B -ne 0) {
                    $nonGrey++
                }
            }
        }
    }
    $bmp.Dispose()
    Write-Host "$($f.Name): nonBlack=$nonBlack nonGrey=$nonGrey maxRGB=($maxR,$maxG,$maxB)"
}
