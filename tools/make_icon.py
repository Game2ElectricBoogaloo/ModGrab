import sys
from PIL import Image, ImageDraw, ImageFont

size = 256
img = Image.new("RGB", (size, size), (24, 26, 33))
draw = ImageDraw.Draw(img)
draw.rounded_rectangle((28, 150, 228, 214), radius=14, fill=(52, 58, 72))
draw.rectangle((108, 36, 148, 116), fill=(255, 205, 60))
draw.polygon([(72, 112), (184, 112), (128, 172)], fill=(255, 205, 60))
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 34)
except OSError:
    font = ImageFont.load_default()
text = "ModGrab"
w = draw.textlength(text, font=font)
draw.text(((size - w) / 2, 206), text, font=font, fill=(235, 238, 245))
img.save(sys.argv[1], quality=92)
