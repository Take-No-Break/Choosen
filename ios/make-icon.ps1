# Draws the iPhone app icon from the Android launcher icon (android/app/src/main/res/drawable/ic_launcher_*.xml).
#   -Palette blue   (AppIcon.appiconset/icon-1024.png): flat blue, two white cards, the blue star
#   -Palette purple (AppIconPurple.appiconset/icon-1024.png, 設定 → アイコンの色): the earlier icon,
#                   deep navy, grey-blue and periwinkle cards, the yellow star
# Windows PowerShell: make-icon.ps1 -Out <png> [-Palette purple]
param([string]$Out, [string]$Palette = 'blue')
Add-Type -AssemblyName System.Drawing
$size = 1024
$bmp = New-Object System.Drawing.Bitmap $size, $size
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$blue = [System.Drawing.Color]::FromArgb(255, 0, 150, 250)
if ($Palette -eq 'purple') {
  $rect = New-Object System.Drawing.Rectangle 0, 0, $size, $size
  $top = [System.Drawing.Color]::FromArgb(255, 0x29, 0x30, 0x50)
  $bottom = [System.Drawing.Color]::FromArgb(255, 0x15, 0x17, 0x24)
  $g.FillRectangle((New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, $top, $bottom, 90), $rect)
  $back = [System.Drawing.Color]::FromArgb(255, 0x5A, 0x64, 0x82)
  $front = [System.Drawing.Color]::FromArgb(255, 0x7D, 0x9B, 0xFF)
  $star = [System.Drawing.Color]::FromArgb(255, 0xF4, 0xB7, 0x3A)
} else {
  $g.Clear($blue)
  $back = [System.Drawing.Color]::FromArgb(0x8C, 255, 255, 255)
  $front = [System.Drawing.Color]::White
  $star = $blue
}

function RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
  $p = New-Object System.Drawing.Drawing2D.GraphicsPath
  $d = 2 * $r
  $p.AddArc($x, $y, $d, $d, 180, 90)
  $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
  $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
  $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
  $p.CloseFigure()
  return $p
}

# viewport 108 -> 1024, then the cards enlarged around (54, 54)
$g.ScaleTransform($size / 108, $size / 108)
$g.TranslateTransform(54, 54)
$g.ScaleTransform(1.3, 1.3)  # larger than on Android: iOS keeps the whole square
$g.TranslateTransform(-54, -54)
$base = $g.Transform.Clone()

# back card: rotated -13 degrees around (49, 52)
$g.TranslateTransform(49, 52); $g.RotateTransform(-13); $g.TranslateTransform(-49, -52)
$g.FillPath((New-Object System.Drawing.SolidBrush $back), (RoundRect 33 30 32 44 4))
$g.Transform = $base.Clone()

# front card: rotated 9 degrees around (62, 54), with the star
$g.TranslateTransform(62, 54); $g.RotateTransform(9); $g.TranslateTransform(-62, -54)
$g.FillPath((New-Object System.Drawing.SolidBrush $front), (RoundRect 45 31 34 46 4))
$g.TranslateTransform(51.8, 43.9); $g.ScaleTransform(0.85, 0.85)
$pts = @(@(12, 2.8), @(14.8, 8.8), @(21.3, 9.5), @(16.4, 13.9), @(17.8, 20.3), @(12, 17), @(6.2, 20.3), @(7.6, 13.9), @(2.7, 9.5), @(9.2, 8.8)) |
  ForEach-Object { New-Object System.Drawing.PointF ([float]$_[0]), ([float]$_[1]) }
$g.FillPolygon((New-Object System.Drawing.SolidBrush $star), [System.Drawing.PointF[]]$pts)

$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
"saved $Out"
