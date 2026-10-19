import os
import struct
import subprocess
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
ASSETS_DIR = ROOT_DIR / "assets"
UI_DIR = ROOT_DIR / "ui"

PS_RENDER_SCRIPT = r"""
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Pt([float]$x, [float]$y) {
    return [System.Drawing.PointF]::new($x, $y)
}

function Col([int]$a, [int]$r, [int]$g, [int]$b) {
    return [System.Drawing.Color]::FromArgb($a, $r, $g, $b)
}

function New-RoundedRectPath([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $d = $r * 2.0
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-ControllerBodyPath([float]$cx, [float]$cy, [float]$scale) {
    # Authentic C1-continuous SNES dog-bone silhouette:
    # Left and right lobes are full 180-degree semicircles on the outside (from 90 deg bottom to 270 deg top),
    # joined by a flat top bridge and a smooth concave bottom bridge.
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $lobeR = 176.0 * $scale
    $offsetX = 166.0 * $scale
    $leftCx = $cx - $offsetX
    $rightCx = $cx + $offsetX

    # Left lobe semicircle: starts at bottom (90 deg) and sweeps 180 deg clockwise to top (270 deg)
    $path.AddArc($leftCx - $lobeR, $cy - $lobeR, $lobeR * 2.0, $lobeR * 2.0, 90.0, 180.0)
    # Top horizontal tangent bridge from (leftCx, cy - lobeR) to (rightCx, cy - lobeR)
    $path.AddLine($leftCx, $cy - $lobeR, $rightCx, $cy - $lobeR)
    # Right lobe semicircle: starts at top (270 deg) and sweeps 180 deg clockwise to bottom (90 deg)
    $path.AddArc($rightCx - $lobeR, $cy - $lobeR, $lobeR * 2.0, $lobeR * 2.0, 270.0, 180.0)
    # Smooth bottom arch from (rightCx, cy + lobeR) back to (leftCx, cy + lobeR)
    $pStart = Pt $rightCx ($cy + $lobeR)
    $c1 = Pt ($cx + 85.0 * $scale) ($cy + $lobeR - 30.0 * $scale)
    $c2 = Pt ($cx - 85.0 * $scale) ($cy + $lobeR - 30.0 * $scale)
    $pEnd = Pt $leftCx ($cy + $lobeR)
    $path.AddBezier($pStart, $c1, $c2, $pEnd)
    $path.CloseFigure()
    return $path
}

function Render-MasterIcon([bool]$compactMode) {
    $size = 1024
    $bmp = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.Clear([System.Drawing.Color]::Transparent)

    # 1. Outer ambient drop shadow for the squircle badge
    $badgeMargin = if ($compactMode) { 36.0 } else { 56.0 }
    $badgeSize = $size - ($badgeMargin * 2.0)
    $badgeRadius = if ($compactMode) { 230.0 } else { 216.0 }

    if (-not $compactMode) {
        for ($i = 16; $i -ge 1; $i--) {
            $alpha = [int](115.0 / ($i * 0.85 + 2.0))
            $pad = $badgeMargin - $i * 1.6
            $w = $size - ($pad * 2.0)
            $shPath = New-RoundedRectPath $pad ($pad + $i * 1.1) $w $w ($badgeRadius + $i)
            $shBrush = [System.Drawing.SolidBrush]::new((Col $alpha 6 8 22))
            $g.FillPath($shBrush, $shPath)
            $shBrush.Dispose()
            $shPath.Dispose()
        }
    }

    # 2. Main Squircle Badge Background
    $badgePath = New-RoundedRectPath $badgeMargin $badgeMargin $badgeSize $badgeSize $badgeRadius

    $bgGrad = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        (Pt 0.0 $badgeMargin),
        (Pt $size ($size - $badgeMargin)),
        (Col 255 26 31 58),
        (Col 255 10 12 24)
    )
    $g.FillPath($bgGrad, $badgePath)
    $bgGrad.Dispose()

    # Radial ambient glow inside the badge (top-left indigo & bottom-right violet-magenta)
    $g.SetClip($badgePath)

    $glowPath1 = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $glowPath1.AddEllipse(-120, -140, 900, 900)
    $glowBrush1 = [System.Drawing.Drawing2D.PathGradientBrush]::new($glowPath1)
    $glowBrush1.CenterColor = Col 105 99 102 241
    $glowBrush1.SurroundColors = [System.Drawing.Color[]]@((Col 0 99 102 241))
    $g.FillPath($glowBrush1, $glowPath1)
    $glowBrush1.Dispose()
    $glowPath1.Dispose()

    $glowPath2 = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $glowPath2.AddEllipse(240, 240, 940, 940)
    $glowBrush2 = [System.Drawing.Drawing2D.PathGradientBrush]::new($glowPath2)
    $glowBrush2.CenterColor = Col 95 168 85 247
    $glowBrush2.SurroundColors = [System.Drawing.Color[]]@((Col 0 168 85 247))
    $g.FillPath($glowBrush2, $glowPath2)
    $glowBrush2.Dispose()
    $glowPath2.Dispose()

    if (-not $compactMode) {
        for ($y = 120; $y -lt 960; $y += 28) {
            $scanPen = [System.Drawing.Pen]::new((Col 11 148 163 255), 2.0)
            $g.DrawLine($scanPen, 50, $y, 974, $y)
            $scanPen.Dispose()
        }
    }

    # Top glossy glass reflection arc
    $glassPath = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $glassPath.AddEllipse(-100, -320, 1224, 680)
    $glassGrad = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        (Pt 512.0 50.0),
        (Pt 512.0 380.0),
        (Col 42 255 255 255),
        (Col 2 255 255 255)
    )
    $g.FillPath($glassGrad, $glassPath)
    $glassGrad.Dispose()
    $glassPath.Dispose()

    $g.ResetClip()

    # Badge glowing neon border (Indigo -> Violet)
    $borderWidth = if ($compactMode) { 24.0 } else { 15.0 }
    $borderGrad = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        (Pt $badgeMargin $badgeMargin),
        (Pt ($size - $badgeMargin) ($size - $badgeMargin)),
        (Col 255 129 140 248),
        (Col 255 168 85 247)
    )
    $borderPen = [System.Drawing.Pen]::new($borderGrad, $borderWidth)
    $g.DrawPath($borderPen, $badgePath)
    $borderPen.Dispose()
    $borderGrad.Dispose()

    # Inner crisp highlight rim
    $innerBadgePath = New-RoundedRectPath ($badgeMargin + 10.0) ($badgeMargin + 10.0) ($badgeSize - 20.0) ($badgeSize - 20.0) ($badgeRadius - 10.0)
    $innerPen = [System.Drawing.Pen]::new((Col 55 255 255 255), 3.0)
    $g.DrawPath($innerPen, $innerBadgePath)
    $innerPen.Dispose()
    $innerBadgePath.Dispose()

    # =========================================================================
    # 3. SNES Controller Illustration
    # =========================================================================
    $cx = 512.0
    $cy = if ($compactMode) { 526.0 } else { 494.0 }
    $scale = if ($compactMode) { 1.24 } else { 1.12 }

    # Controller ambient drop shadow
    for ($i = 14; $i -ge 1; $i--) {
        $shBody = New-ControllerBodyPath $cx ($cy + $i * 2.2 + 8.0) ($scale + $i * 0.004)
        $shAlpha = [int](140.0 / ($i * 0.8 + 1.5))
        $shBrush = [System.Drawing.SolidBrush]::new((Col $shAlpha 4 6 16))
        $g.FillPath($shBrush, $shBody)
        $shBrush.Dispose()
        $shBody.Dispose()
    }

    # Shoulder Buttons (L and R) peeking above the left and right lobes
    $lShoulderPath = New-RoundedRectPath ($cx - 296.0 * $scale) ($cy - 206.0 * $scale) (158.0 * $scale) (60.0 * $scale) (26.0 * $scale)
    $rShoulderPath = New-RoundedRectPath ($cx + 138.0 * $scale) ($cy - 206.0 * $scale) (158.0 * $scale) (60.0 * $scale) (26.0 * $scale)
    $shoulderGrad = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        (Pt $cx ($cy - 210.0 * $scale)),
        (Pt $cx ($cy - 140.0 * $scale)),
        (Col 255 208 215 234),
        (Col 255 126 135 162)
    )
    $shoulderPen = [System.Drawing.Pen]::new((Col 235 34 40 66), 7.0)
    $g.FillPath($shoulderGrad, $lShoulderPath)
    $g.DrawPath($shoulderPen, $lShoulderPath)
    $g.FillPath($shoulderGrad, $rShoulderPath)
    $g.DrawPath($shoulderPen, $rShoulderPath)
    $shoulderGrad.Dispose()
    $shoulderPen.Dispose()
    $lShoulderPath.Dispose()
    $rShoulderPath.Dispose()

    # Main Controller Body Shell (Warm Super Nintendo Light Metallic Silver-Grey)
    $bodyPath = New-ControllerBodyPath $cx $cy $scale
    $bodyGrad = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        (Pt $cx ($cy - 185.0 * $scale)),
        (Pt $cx ($cy + 185.0 * $scale)),
        (Col 255 240 244 253),
        (Col 255 174 182 206)
    )
    $g.FillPath($bodyGrad, $bodyPath)
    $bodyGrad.Dispose()

    # Controller top bevel highlight & purple bridge accent
    $g.SetClip($bodyPath)
    $rimPen = [System.Drawing.Pen]::new((Col 200 255 255 255), 14.0)
    $g.DrawPath($rimPen, $bodyPath)
    $rimPen.Dispose()

    $bridgeBand = New-RoundedRectPath ($cx - 88.0 * $scale) ($cy - 142.0 * $scale) (176.0 * $scale) (22.0 * $scale) (11.0 * $scale)
    $bandBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        (Pt ($cx - 90.0 * $scale) $cy),
        (Pt ($cx + 90.0 * $scale) $cy),
        (Col 255 99 102 241),
        (Col 255 168 85 247)
    )
    $g.FillPath($bandBrush, $bridgeBand)
    $bandBrush.Dispose()
    $bridgeBand.Dispose()
    $g.ResetClip()

    $bodyOutlinePen = [System.Drawing.Pen]::new((Col 255 32 38 64), 9.0)
    $g.DrawPath($bodyOutlinePen, $bodyPath)
    $bodyOutlinePen.Dispose()
    $bodyPath.Dispose()

    # Left Lobe Recessed Circle (around D-Pad)
    $leftCx = $cx - 166.0 * $scale
    $rightCx = $cx + 166.0 * $scale
    $lobeCircleR = 130.0 * $scale

    $leftRecessBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        (Pt $leftCx ($cy - $lobeCircleR)),
        (Pt $leftCx ($cy + $lobeCircleR)),
        (Col 255 162 170 196),
        (Col 255 210 216 234)
    )
    $g.FillEllipse($leftRecessBrush, $leftCx - $lobeCircleR, $cy - $lobeCircleR, $lobeCircleR * 2.0, $lobeCircleR * 2.0)
    $recessRimPen = [System.Drawing.Pen]::new((Col 155 108 116 144), 4.5)
    $g.DrawEllipse($recessRimPen, $leftCx - $lobeCircleR, $cy - $lobeCircleR, $lobeCircleR * 2.0, $lobeCircleR * 2.0)
    $leftRecessBrush.Dispose()
    $recessRimPen.Dispose()

    # Right Lobe Dark Charcoal Circle (Iconic SNES Action Button Island)
    $rightIslandBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        (Pt ($rightCx - $lobeCircleR) ($cy - $lobeCircleR)),
        (Pt ($rightCx + $lobeCircleR) ($cy + $lobeCircleR)),
        (Col 255 70 78 104),
        (Col 255 38 44 66)
    )
    $g.FillEllipse($rightIslandBrush, $rightCx - $lobeCircleR, $cy - $lobeCircleR, $lobeCircleR * 2.0, $lobeCircleR * 2.0)
    $islandRimPen = [System.Drawing.Pen]::new((Col 235 24 28 46), 6.5)
    $g.DrawEllipse($islandRimPen, $rightCx - $lobeCircleR, $cy - $lobeCircleR, $lobeCircleR * 2.0, $lobeCircleR * 2.0)
    $rightIslandBrush.Dispose()
    $islandRimPen.Dispose()

    # Two diagonal pill capsules connecting X-Y and A-B on the right button island
    $state = $g.Save()
    $g.TranslateTransform($rightCx, $cy)
    $g.RotateTransform(-36.0)
    $pillW = 172.0 * $scale
    $pillH = 70.0 * $scale
    $pillOffset = 46.0 * $scale
    $pillBrush = [System.Drawing.SolidBrush]::new((Col 255 174 182 206))
    $pillPen = [System.Drawing.Pen]::new((Col 195 28 34 52), 3.8)

    $pillTop = New-RoundedRectPath (-$pillW / 2.0) (-$pillOffset - $pillH / 2.0) $pillW $pillH ($pillH / 2.0)
    $pillBot = New-RoundedRectPath (-$pillW / 2.0) ($pillOffset - $pillH / 2.0) $pillW $pillH ($pillH / 2.0)
    $g.FillPath($pillBrush, $pillTop)
    $g.DrawPath($pillPen, $pillTop)
    $g.FillPath($pillBrush, $pillBot)
    $g.DrawPath($pillPen, $pillBot)
    $pillTop.Dispose()
    $pillBot.Dispose()
    $pillBrush.Dispose()
    $pillPen.Dispose()
    $g.Restore($state)

    # =========================================================================
    # 4. Crisp Cross D-Pad on Left Lobe
    # =========================================================================
    $dpadArmW = 60.0 * $scale
    $dpadArmL = 176.0 * $scale
    $dpadR = 14.0 * $scale

    $dpadHShadow = New-RoundedRectPath ($leftCx - $dpadArmL / 2.0) ($cy - $dpadArmW / 2.0 + 6.0) $dpadArmL $dpadArmW $dpadR
    $dpadVShadow = New-RoundedRectPath ($leftCx - $dpadArmW / 2.0) ($cy - $dpadArmL / 2.0 + 6.0) $dpadArmW $dpadArmL $dpadR
    $dpadShBrush = [System.Drawing.SolidBrush]::new((Col 115 15 20 35))
    $g.FillPath($dpadShBrush, $dpadHShadow)
    $g.FillPath($dpadShBrush, $dpadVShadow)
    $dpadShBrush.Dispose()
    $dpadHShadow.Dispose()
    $dpadVShadow.Dispose()

    $dpadH = New-RoundedRectPath ($leftCx - $dpadArmL / 2.0) ($cy - $dpadArmW / 2.0) $dpadArmL $dpadArmW $dpadR
    $dpadV = New-RoundedRectPath ($leftCx - $dpadArmW / 2.0) ($cy - $dpadArmL / 2.0) $dpadArmW $dpadArmL $dpadR
    $dpadGrad = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        (Pt $leftCx ($cy - $dpadArmL / 2.0)),
        (Pt $leftCx ($cy + $dpadArmL / 2.0)),
        (Col 255 64 72 96),
        (Col 255 32 37 54)
    )
    $dpadBorderPen = [System.Drawing.Pen]::new((Col 255 18 22 36), 5.5)
    $g.DrawPath($dpadBorderPen, $dpadH)
    $g.DrawPath($dpadBorderPen, $dpadV)
    $g.FillPath($dpadGrad, $dpadH)
    $g.FillPath($dpadGrad, $dpadV)
    $dpadGrad.Dispose()
    $dpadBorderPen.Dispose()
    $dpadH.Dispose()
    $dpadV.Dispose()

    # D-Pad center pivot indent + directional triangles
    $pivotR = 18.0 * $scale
    $pivotBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        (Pt ($leftCx - $pivotR) ($cy - $pivotR)),
        (Pt ($leftCx + $pivotR) ($cy + $pivotR)),
        (Col 255 24 28 42),
        (Col 255 58 65 88)
    )
    $g.FillEllipse($pivotBrush, $leftCx - $pivotR, $cy - $pivotR, $pivotR * 2.0, $pivotR * 2.0)
    $pivotBrush.Dispose()

    $arrowBrush = [System.Drawing.SolidBrush]::new((Col 185 156 170 206))
    $aw = 13.5 * $scale
    $ah = 16.5 * $scale
    $ad = 66.0 * $scale
    $g.FillPolygon($arrowBrush, [System.Drawing.PointF[]]@(
        (Pt $leftCx ($cy - $ad - $ah)),
        (Pt ($leftCx - $aw) ($cy - $ad + 4.0)),
        (Pt ($leftCx + $aw) ($cy - $ad + 4.0))
    ))
    $g.FillPolygon($arrowBrush, [System.Drawing.PointF[]]@(
        (Pt $leftCx ($cy + $ad + $ah)),
        (Pt ($leftCx - $aw) ($cy + $ad - 4.0)),
        (Pt ($leftCx + $aw) ($cy + $ad - 4.0))
    ))
    $g.FillPolygon($arrowBrush, [System.Drawing.PointF[]]@(
        (Pt ($leftCx - $ad - $ah) $cy),
        (Pt ($leftCx - $ad + 4.0) ($cy - $aw)),
        (Pt ($leftCx - $ad + 4.0) ($cy + $aw))
    ))
    $g.FillPolygon($arrowBrush, [System.Drawing.PointF[]]@(
        (Pt ($leftCx + $ad + $ah) $cy),
        (Pt ($leftCx + $ad - 4.0) ($cy - $aw)),
        (Pt ($leftCx + $ad - 4.0) ($cy + $aw))
    ))
    $arrowBrush.Dispose()

    # =========================================================================
    # 5. Center Angled SELECT & START Pill Buttons
    # =========================================================================
    foreach ($offset in @(-26.0, 26.0)) {
        $st = $g.Save()
        $g.TranslateTransform(($cx + $offset * $scale), ($cy + 26.0 * $scale))
        $g.RotateTransform(-34.0)
        $sw = 44.0 * $scale
        $sh = 17.0 * $scale
        $sPill = New-RoundedRectPath (-$sw / 2.0) (-$sh / 2.0) $sw $sh ($sh / 2.0)
        $sBrush = [System.Drawing.SolidBrush]::new((Col 255 50 57 80))
        $sPen = [System.Drawing.Pen]::new((Col 215 22 26 40), 3.0)
        $g.FillPath($sBrush, $sPill)
        $g.DrawPath($sPen, $sPill)
        $sPill.Dispose()
        $sBrush.Dispose()
        $sPen.Dispose()
        $g.Restore($st)
    }

    # =========================================================================
    # 6. Four Iconic 4-Color SNES Action Buttons (X=Blue, Y=Green, A=Red, B=Yellow)
    # =========================================================================
    $rad = -36.0 * [Math]::PI / 180.0
    $cosA = [Math]::Cos($rad)
    $sinA = [Math]::Sin($rad)

    $buttons = @(
        @{ Name="X"; RX=50.0;  RY=-46.0; Top=(Col 255 96 165 250);  Bot=(Col 255 29 78 216) },
        @{ Name="Y"; RX=-50.0; RY=-46.0; Top=(Col 255 74 222 128);  Bot=(Col 255 21 128 61) },
        @{ Name="A"; RX=50.0;  RY=46.0;  Top=(Col 255 251 113 133); Bot=(Col 255 190 18 60) },
        @{ Name="B"; RX=-50.0; RY=46.0;  Top=(Col 255 250 204 21);  Bot=(Col 255 180 83 9) }
    )

    $btnR = 29.0 * $scale
    foreach ($btn in $buttons) {
        $bx = [float]($rightCx + ($btn.RX * $cosA - $btn.RY * $sinA) * $scale)
        $by = [float]($cy + ($btn.RX * $sinA + $btn.RY * $cosA) * $scale)

        # Button drop shadow
        $shBrush = [System.Drawing.SolidBrush]::new((Col 150 12 16 28))
        $g.FillEllipse($shBrush, $bx - $btnR, $by - $btnR + 5.0, $btnR * 2.0, $btnR * 2.0)
        $shBrush.Dispose()

        # Button 3D dome fill
        $bGrad = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
            (Pt ($bx - $btnR) ($by - $btnR)),
            (Pt ($bx + $btnR) ($by + $btnR)),
            $btn.Top,
            $btn.Bot
        )
        $g.FillEllipse($bGrad, $bx - $btnR, $by - $btnR, $btnR * 2.0, $btnR * 2.0)
        $bGrad.Dispose()

        # Button dark crisp outline
        $bPen = [System.Drawing.Pen]::new((Col 240 16 20 34), 4.5)
        $g.DrawEllipse($bPen, $bx - $btnR, $by - $btnR, $btnR * 2.0, $btnR * 2.0)
        $bPen.Dispose()

        # Specular glossy highlight on top-left of button
        $specBrush = [System.Drawing.SolidBrush]::new((Col 165 255 255 255))
        $g.FillEllipse($specBrush, $bx - $btnR * 0.55, $by - $btnR * 0.72, $btnR * 0.95, $btnR * 0.50)
        $specBrush.Dispose()
    }

    if (-not $compactMode) {
        # Top Neon Four-Color Gem Emblem
        $emblemCy = 186.0
        $emblemR = 17.5
        $emblemDots = @(
            @{ X=512.0; Y=($emblemCy - 36.0); C=(Col 255 96 165 250) },
            @{ X=472.0; Y=$emblemCy;          C=(Col 255 74 222 128) },
            @{ X=552.0; Y=$emblemCy;          C=(Col 255 248 113 113) },
            @{ X=512.0; Y=($emblemCy + 36.0); C=(Col 255 250 204 21) }
        )
        foreach ($d in $emblemDots) {
            $glowB = [System.Drawing.SolidBrush]::new((Col 85 $d.C.R $d.C.G $d.C.B))
            $g.FillEllipse($glowB, [float]($d.X - $emblemR * 1.7), [float]($d.Y - $emblemR * 1.7), [float]($emblemR * 3.4), [float]($emblemR * 3.4))
            $glowB.Dispose()

            $dotB = [System.Drawing.SolidBrush]::new($d.C)
            $g.FillEllipse($dotB, [float]($d.X - $emblemR), [float]($d.Y - $emblemR), [float]($emblemR * 2.0), [float]($emblemR * 2.0))
            $dotB.Dispose()
        }

        # Bottom glowing pill badge "SNEX9X v2"
        $tagW = 394.0
        $tagH = 82.0
        $tagX = 512.0 - $tagW / 2.0
        $tagY = 796.0
        $tagPath = New-RoundedRectPath $tagX $tagY $tagW $tagH 41.0
        $tagBg = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
            (Pt $tagX $tagY),
            (Pt ($tagX + $tagW) ($tagY + $tagH)),
            (Col 242 34 40 78),
            (Col 242 18 22 44)
        )
        $tagPen = [System.Drawing.Pen]::new((Col 220 129 140 248), 4.5)
        $g.FillPath($tagBg, $tagPath)
        $g.DrawPath($tagPen, $tagPath)
        $tagBg.Dispose()
        $tagPen.Dispose()
        $tagPath.Dispose()

        $font = [System.Drawing.Font]::new("Segoe UI", 40.0, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
        $sf = [System.Drawing.StringFormat]::new()
        $sf.Alignment = [System.Drawing.StringAlignment]::Center
        $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
        $textRect = [System.Drawing.RectangleF]::new($tagX, ($tagY + 2.0), $tagW, $tagH)
        $textBrush = [System.Drawing.SolidBrush]::new((Col 255 243 244 248))
        $g.DrawString("SNEX9X v2", $font, $textBrush, $textRect, $sf)
        $textBrush.Dispose()
        $font.Dispose()
        $sf.Dispose()
    }

    $badgePath.Dispose()
    $g.Dispose()
    return $bmp
}

$outDir = $args[0]
$fullBmp = Render-MasterIcon $false
$compactBmp = Render-MasterIcon $true

$sizes = @(512, 256, 128, 64, 48, 32, 24, 16)
foreach ($s in $sizes) {
    $srcBmp = if ($s -le 48) { $compactBmp } else { $fullBmp }
    $resized = [System.Drawing.Bitmap]::new($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $rg = [System.Drawing.Graphics]::FromImage($resized)
    $rg.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $rg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $rg.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $rg.Clear([System.Drawing.Color]::Transparent)
    $rg.DrawImage($srcBmp, 0, 0, $s, $s)
    $rg.Dispose()
    $outPath = Join-Path $outDir ("icon_" + $s + ".png")
    $resized.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $resized.Dispose()
}
$fullBmp.Dispose()
$compactBmp.Dispose()
"""


def build_multi_size_ico(png_dir: Path, ico_path: Path):
    sizes = [16, 24, 32, 48, 64, 128, 256]
    entries_data = []

    for s in sizes:
        png_file = png_dir / f"icon_{s}.png"
        if s == 256:
            data = png_file.read_bytes()
            entries_data.append((s, s, data))
        else:
            raw_file = png_dir / f"icon_{s}.bgra"
            ps_extract = f"""
Add-Type -AssemblyName System.Drawing
$b = [System.Drawing.Bitmap]::new("{png_file}")
$ms = [System.IO.MemoryStream]::new()
for ($y = $b.Height - 1; $y -ge 0; $y--) {{
    for ($x = 0; $x -lt $b.Width; $x++) {{
        $c = $b.GetPixel($x, $y)
        $ms.WriteByte($c.B)
        $ms.WriteByte($c.G)
        $ms.WriteByte($c.R)
        $ms.WriteByte($c.A)
    }}
}}
[System.IO.File]::WriteAllBytes("{raw_file}", $ms.ToArray())
$ms.Dispose()
$b.Dispose()
"""
            subprocess.run(["powershell", "-NoProfile", "-Command", ps_extract], check=True)
            bgra = raw_file.read_bytes()
            raw_file.unlink(missing_ok=True)

            row_bytes = ((s + 31) // 32) * 4
            and_mask = bytearray(row_bytes * s)
            for y in range(s):
                for x in range(s):
                    alpha = bgra[(y * s + x) * 4 + 3]
                    if alpha < 16:
                        byte_idx = y * row_bytes + (x // 8)
                        bit_idx = 7 - (x % 8)
                        and_mask[byte_idx] |= (1 << bit_idx)

            bih = struct.pack(
                "<IIIHHIIIIII",
                40,
                s,
                s * 2,
                1,
                32,
                0,
                len(bgra) + len(and_mask),
                0, 0, 0, 0
            )
            dib_data = bih + bgra + bytes(and_mask)
            entries_data.append((s, s, dib_data))

    ico_header = struct.pack("<HHH", 0, 1, len(entries_data))
    dir_entries = bytearray()
    payloads = bytearray()
    offset = 6 + 16 * len(entries_data)

    for w, h, data in entries_data:
        bw = 0 if w == 256 else w
        bh = 0 if h == 256 else h
        dir_entries += struct.pack(
            "<BBBBHHII",
            bw,
            bh,
            0,
            0,
            1,
            32,
            len(data),
            offset
        )
        payloads += data
        offset += len(data)

    ico_path.write_bytes(ico_header + bytes(dir_entries) + bytes(payloads))


def main():
    ASSETS_DIR.mkdir(parents=True, exist_ok=True)
    ps_file = ASSETS_DIR / "_render_icon.ps1"
    ps_file.write_text(PS_RENDER_SCRIPT, encoding="utf-8")
    try:
        subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(ps_file), str(ASSETS_DIR)],
            check=True
        )
    finally:
        ps_file.unlink(missing_ok=True)

    ico_path = ASSETS_DIR / "snex9x.ico"
    build_multi_size_ico(ASSETS_DIR, ico_path)

    preview_png = ASSETS_DIR / "snex9x.png"
    if preview_png.exists():
        preview_png.unlink()
    (ASSETS_DIR / "icon_512.png").replace(preview_png)

    ui_icon = UI_DIR / "icon.png"
    ui_icon.write_bytes((ASSETS_DIR / "icon_256.png").read_bytes())

    for s in [16, 24, 32, 48, 64, 128, 256]:
        (ASSETS_DIR / f"icon_{s}.png").unlink(missing_ok=True)

    print(f"Generated {ico_path} ({ico_path.stat().st_size} bytes) and {preview_png}")


if __name__ == "__main__":
    main()
