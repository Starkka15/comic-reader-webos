# Draws the app's icon into app\images. Run it again after changing the drawing.
#     powershell -File tools\make-icons.ps1
Add-Type -AssemblyName System.Drawing
$out = Join-Path (Split-Path $PSScriptRoot -Parent) "app\images"
New-Item -ItemType Directory -Force $out | Out-Null

function Rgb([int]$r, [int]$g, [int]$b, [int]$a = 255) {
    return [System.Drawing.Color]::FromArgb($a, $r, $g, $b)
}

# A comic page with panels and a speech balloon, over a faint white disc.
function Draw-Icon([int]$size, [string]$name) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $u = $size / 64
    for ($i = 4; $i -ge 1; $i--) {
        $g.FillEllipse((New-Object System.Drawing.SolidBrush (Rgb 0 0 0 10)),
            (3 - $i * 0.5) * $u, (5 - $i * 0.2) * $u, (58 + $i) * $u, (58 + $i) * $u)
    }
    $g.FillEllipse((New-Object System.Drawing.SolidBrush (Rgb 255 255 255 110)), 3 * $u, 3 * $u, 58 * $u, 58 * $u)
    # The page.
    $g.FillRectangle((New-Object System.Drawing.SolidBrush (Rgb 250 248 240)), 16 * $u, 9 * $u, 32 * $u, 46 * $u)
    $ink = New-Object System.Drawing.Pen (Rgb 40 40 46), ([Math]::Max(1, 1.6 * $u))
    $g.DrawRectangle($ink, 16 * $u, 9 * $u, 32 * $u, 46 * $u)
    # Panels.
    $g.FillRectangle((New-Object System.Drawing.SolidBrush (Rgb 232 84 72)), 19 * $u, 12 * $u, 26 * $u, 15 * $u)
    $g.DrawRectangle($ink, 19 * $u, 12 * $u, 26 * $u, 15 * $u)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush (Rgb 74 150 222)), 19 * $u, 30 * $u, 11 * $u, 22 * $u)
    $g.DrawRectangle($ink, 19 * $u, 30 * $u, 11 * $u, 22 * $u)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush (Rgb 250 204 72)), 33 * $u, 30 * $u, 12 * $u, 22 * $u)
    $g.DrawRectangle($ink, 33 * $u, 30 * $u, 12 * $u, 22 * $u)
    # A speech balloon in the top panel.
    $g.FillEllipse((New-Object System.Drawing.SolidBrush (Rgb 255 255 255)), 27 * $u, 14 * $u, 15 * $u, 9 * $u)
    $g.DrawEllipse($ink, 27 * $u, 14 * $u, 15 * $u, 9 * $u)
    $g.Dispose()
    $bmp.Save((Join-Path $out $name), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

Draw-Icon 64 "icon.png"
Draw-Icon 256 "icon-256x256.png"
Get-ChildItem $out | Select-Object Name, Length
