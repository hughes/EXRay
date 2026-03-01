#!/usr/bin/env python3
"""Generate exray-icon.ico from logo PNGs using Pillow."""

from pathlib import Path
from PIL import Image

icons_dir = Path(__file__).parent
out_path = icons_dir / ".." / "resources" / "exray-icon.ico"

sizes = [16, 32, 64, 128, 256]
images = [Image.open(icons_dir / f"logo-{s}.png") for s in sizes]

# Pillow's sizes parameter resizes the source image, so we skip it
# and just pass pre-rendered PNGs via append_images.
images[-1].save(out_path, format="ICO", append_images=images[:-1])
print(f"Created {out_path.resolve()}")
