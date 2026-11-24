# Turns sim/out/*.ppm into upscaled PNGs and one contact sheet.
import sys, glob, os
from PIL import Image
d = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
names = sorted(glob.glob(os.path.join(d, "*.ppm")))
if len(sys.argv) > 1: names = [n for n in names if any(a in os.path.basename(n) for a in sys.argv[1:])]
S = 3; cols = 2
ims = [Image.open(n).convert("RGB").resize((240 * S, 135 * S), Image.NEAREST) for n in names]
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (240 * S + 8) + 8, rows * (135 * S + 8) + 8), (40, 40, 40))
for i, im in enumerate(ims):
    sheet.paste(im, (8 + (i % cols) * (240 * S + 8), 8 + (i // cols) * (135 * S + 8)))
sheet.save(os.path.join(d, "sheet.png"))
print(len(ims), "frames ->", os.path.join(d, "sheet.png"), sheet.size)
