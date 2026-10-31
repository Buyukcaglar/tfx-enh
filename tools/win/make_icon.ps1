# The program's icon from a PNG: 256, 64, 48, 32, 24 and 16 pixel images
# (high-quality bicubic, each a PNG inside the ICO, as Windows Vista and
# later read them). The artwork is the user's and stays in private/ (the
# build embeds private/icon/tfx.ico when it exists: CMakeLists.txt).
#
#   powershell -File tools/win/make_icon.ps1 SOURCE.png private/icon/tfx.ico
param([Parameter(Mandatory)] [string]$Source, [Parameter(Mandatory)] [string]$Out)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile((Resolve-Path $Source))
$sizes = 256, 64, 48, 32, 24, 16
$images = foreach ($n in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $n, $n, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($src, 0, 0, $n, $n)
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    , $ms.ToArray()
}
$src.Dispose()
New-Item -ItemType Directory -Force (Split-Path -Parent $Out) | Out-Null
$f = New-Object System.IO.BinaryWriter ([System.IO.File]::Create($Out))
$f.Write([uint16]0); $f.Write([uint16]1); $f.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $n = $sizes[$i]
    $f.Write([byte]($n % 256)); $f.Write([byte]($n % 256)) # 256 is written 0
    $f.Write([byte]0); $f.Write([byte]0)
    $f.Write([uint16]1); $f.Write([uint16]32)
    $f.Write([uint32]$images[$i].Length); $f.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($b in $images) { $f.Write($b) }
$f.Close()
"wrote $Out ($offset bytes, sizes $($sizes -join ' '))"
