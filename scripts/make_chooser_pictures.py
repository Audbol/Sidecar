"""Scales the surface screenshots to the start-up chooser's tile pictures (resources/chooser/*.png).

Usage: python scripts/make_chooser_pictures.py <folder with the screenshot tests' pictures>
The pictures are Sidecar's own drawings (the screenshot tests render them)."""
import os, sys
from PIL import Image

SOURCES = {  # tile name: picture (the folder, else docs/images)
    "wing": "console.png", "universal": "universal-mixer.png", "x32": "x32-surface.png", "promix": "promix-surface.png",
    "y02r": "y02r-surface.png", "sq5": "sq5-home.png", "y01v": "y01v-surface.png", "dlive": "dlive-surface.png",
    "dm2000": "dm2000.png", "pro2": "pro2.png", "xenyx1202": "xenyxq1202.png",
}
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
folder = sys.argv[1]
out = os.path.join(root, "resources", "chooser")
os.makedirs(out, exist_ok=True)
for name, src in SOURCES.items():
    path = os.path.join(folder, src)
    if not os.path.exists(path):
        path = os.path.join(root, "docs", "images", src)
    img = Image.open(path).convert("RGB")
    img.thumbnail((360, 200), Image.LANCZOS)
    img.save(os.path.join(out, name + ".png"), optimize=True)
