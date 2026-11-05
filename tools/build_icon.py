import os
import re
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
ASSETS_DIR = ROOT_DIR / "assets"
UI_DIR = ROOT_DIR / "ui"
CHROME_PATH = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
BRAIN_SVG = Path(r"C:\Users\Rodrigo\.gemini\antigravity\brain\0f866ec9-63cc-45bb-8c70-f270a718caaf\snes_controller_figma.svg")


def prepare_svg(assets_dir: Path) -> Path:
    """Ensure assets/snes_controller.svg exists with transparent background."""
    dest_svg = assets_dir / "snes_controller.svg"
    if BRAIN_SVG.exists():
        raw_svg = BRAIN_SVG.read_text(encoding="utf-8")
        clean_svg = re.sub(r'<rect[^>]*fill="#F2F2F2"[^>]*/>\s*', '', raw_svg, flags=re.IGNORECASE)
        dest_svg.write_text(clean_svg, encoding="utf-8")
    elif not dest_svg.exists():
        raise FileNotFoundError(f"Source SVG not found at {BRAIN_SVG} or {dest_svg}")
    return dest_svg


def render_master_png(svg_path: Path, out_png: Path):
    """Render transparent 1024x1024 PNG from SVG using headless Chrome."""
    svg_content = svg_path.read_text(encoding="utf-8")
    html_content = f"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
* {{ margin: 0; padding: 0; box-sizing: border-box; }}
html, body {{
  width: 1024px;
  height: 1024px;
  background: transparent;
  overflow: hidden;
  display: flex;
  align-items: center;
  justify-content: center;
}}
svg {{
  width: 960px;
  height: auto;
  filter: drop-shadow(0 28px 36px rgba(10, 15, 30, 0.45));
}}
</style>
</head>
<body>
{svg_content}
</body>
</html>"""

    with tempfile.NamedTemporaryFile("w", suffix=".html", delete=False, encoding="utf-8") as tf:
        tf.write(html_content)
        temp_html = Path(tf.name)

    try:
        cmd = [
            CHROME_PATH,
            "--headless",
            "--disable-gpu",
            "--default-background-color=00000000",
            "--window-size=1024,1024",
            f"--screenshot={out_png}",
            temp_html.as_uri()
        ]
        subprocess.run(cmd, check=True)
    finally:
        temp_html.unlink(missing_ok=True)


def downsample_images(master_png: Path, out_dir: Path, sizes: list):
    """Use PowerShell + System.Drawing for high-quality bicubic downsampling."""
    sizes_str = ",".join(str(s) for s in sizes)
    ps_script = f"""
Add-Type -AssemblyName System.Drawing
$master = [System.Drawing.Bitmap]::FromFile('{master_png}')
$sizes = @({sizes_str})

foreach ($s in $sizes) {{
    $resized = [System.Drawing.Bitmap]::new($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($resized)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($master, 0, 0, $s, $s)
    $g.Dispose()

    $outPath = Join-Path '{out_dir}' ("icon_" + $s + ".png")
    $resized.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $resized.Dispose()
}}
$master.Dispose()
"""
    subprocess.run(["powershell", "-NoProfile", "-Command", ps_script], check=True)


def build_multi_size_ico(png_dir: Path, ico_path: Path):
    """
    Pack PNG frames (16, 24, 32, 48, 64, 128, 256) into a valid Windows .ico file.
    Sizes < 256 are encoded as 32-bit BGRA DIBs with 1-bit transparency mask.
    Size 256 is stored as PNG compressed (Vista/Win7/Win10/Win11 standard).
    """
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
$b = [System.Drawing.Bitmap]::new('{png_file}')
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
[System.IO.File]::WriteAllBytes('{raw_file}', $ms.ToArray())
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
                40,             # biSize
                s,              # biWidth
                s * 2,          # biHeight (doubled for ICO DIB)
                1,              # biPlanes
                32,             # biBitCount
                0,              # biCompression (BI_RGB)
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
    svg_path = prepare_svg(ASSETS_DIR)
    print(f"[1/4] Prepared SVG: {svg_path} ({svg_path.stat().st_size} bytes)")

    master_png = ASSETS_DIR / "_master_1024.png"
    render_master_png(svg_path, master_png)
    print(f"[2/4] Rendered master 1024x1024 PNG: {master_png.stat().st_size} bytes")

    sizes = [16, 24, 32, 48, 64, 128, 256, 512]
    downsample_images(master_png, ASSETS_DIR, sizes)
    print("[3/4] Downsampled all icon sizes (16, 24, 32, 48, 64, 128, 256, 512)")

    ico_path = ASSETS_DIR / "snex9x.ico"
    build_multi_size_ico(ASSETS_DIR, ico_path)

    # 512x512 preview
    preview_png = ASSETS_DIR / "snex9x.png"
    if preview_png.exists():
        preview_png.unlink()
    (ASSETS_DIR / "icon_512.png").replace(preview_png)

    # 256x256 UI favicon
    ui_icon = UI_DIR / "icon.png"
    ui_icon.write_bytes((ASSETS_DIR / "icon_256.png").read_bytes())

    # Clean intermediate files
    master_png.unlink(missing_ok=True)
    for s in [16, 24, 32, 48, 64, 128, 256]:
        (ASSETS_DIR / f"icon_{s}.png").unlink(missing_ok=True)

    print(f"[4/4] Successfully generated {ico_path} ({ico_path.stat().st_size} bytes), {preview_png}, and {ui_icon}")


if __name__ == "__main__":
    main()
