# Build assets/launcher/icon.ico (the Windows exe icon) from the golden ball:
#   powershell -NoProfile -File tools/build_windows_icon.ps1 [source.png]
# The source is centred on a transparent square and stored at the usual icon
# sizes as PNG entries (Windows Vista+).
param([string]$Source = "$PSScriptRoot\..\assets\launcher\ball.png",
      [string]$Out = "$PSScriptRoot\..\assets\launcher\icon.ico")
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile((Resolve-Path $Source))
$side = [Math]::Max($src.Width, $src.Height)
$entries = foreach ($size in 16, 24, 32, 48, 64, 128, 256) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $scale = $size / $side
    $w = $src.Width * $scale; $h = $src.Height * $scale
    $g.DrawImage($src, ($size - $w) / 2, ($size - $h) / 2, $w, $h)
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    [pscustomobject]@{ Size = $size; Data = $ms.ToArray() }
}
$src.Dispose()

$ico = New-Object System.IO.MemoryStream
$wr = New-Object System.IO.BinaryWriter $ico
$wr.Write([uint16]0); $wr.Write([uint16]1); $wr.Write([uint16]$entries.Count)
$offset = 6 + 16 * $entries.Count
foreach ($e in $entries) {
    $dim = if ($e.Size -ge 256) { 0 } else { $e.Size }      # 0 means 256
    $wr.Write([byte]$dim); $wr.Write([byte]$dim); $wr.Write([byte]0); $wr.Write([byte]0)
    $wr.Write([uint16]1); $wr.Write([uint16]32)
    $wr.Write([uint32]$e.Data.Length); $wr.Write([uint32]$offset)
    $offset += $e.Data.Length
}
foreach ($e in $entries) { $wr.Write($e.Data) }
$wr.Flush()
[System.IO.File]::WriteAllBytes([System.IO.Path]::GetFullPath($Out), $ico.ToArray())
"Wrote $Out ($($ico.Length) bytes)"
