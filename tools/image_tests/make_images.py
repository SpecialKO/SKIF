# Makes the test images of tools/image_tests (synthetic, so they can be shared freely). Needs Pillow (pip install pillow).
# Only needed to make them again: the images are part of the repository.
#
#   python tools/image_tests/make_images.py

import os
from PIL import Image, ImageDraw

W, H = 60, 90
OUT  = os.path.join (os.path.dirname (os.path.abspath (__file__)), 'images')
os.makedirs (OUT, exist_ok = True)

def path (name):
    return os.path.join (OUT, name)

def frames (count, alpha = False):
    out = []
    for i in range (count):
        im = Image.new ('RGBA', (W, H), (20, 40, 60, 0 if alpha else 255))
        d  = ImageDraw.Draw (im)
        d.rectangle ([5 + i * 3, 10, 25 + i * 3, 40], fill = (200, i * 30 % 255, 50, 128 + i * 10 if alpha else 255))
        d.ellipse   ([30, 50 + i, 50, 70 + i],        fill = (10, 200, 100, 255))
        out.append (im)
    return out

# APNG: delays (incl. 0 and 5 ms: shown for 100 ms), blend over, a mix of blend and dispose ops, a default image
#   that is not part of the animation, a palette
f  = frames (6)
f [0].save (path ('apng_basic.png'),   save_all = True, append_images = f [1:], duration = [50, 100, 0, 5, 200, 70], loop = 0)
fa = frames (6, True)
fa[0].save (path ('apng_blend.png'),   save_all = True, append_images = fa[1:], duration = 80, loop = 0, blend = 1, disposal = 0)
fa[0].save (path ('apng_dispose.png'), save_all = True, append_images = fa[1:], duration = 80, loop = 0, blend = [1, 0, 1, 1, 0, 1],
            disposal = [0, 2, 1, 2, 0, 2])
f [0].save (path ('apng_default.png'), save_all = True, append_images = f [1:], duration = 80, loop = 0, default_image = True)
pal = Image.new ('P', (1, 1))
pal.putpalette ([20, 40, 60,  200, 0, 50,  10, 200, 100,  255, 255, 0] + [0] * (252 * 3))
fp = []
for i in range (6):
    im = Image.new ('RGB', (W, H), (20, 40, 60)); d = ImageDraw.Draw (im)
    d.rectangle ([5 + i * 3, 10, 25 + i * 3, 40], fill = (200, 0, 50)); d.ellipse ([30, 50 + i, 50, 70 + i], fill = (10, 200, 100))
    fp.append (im.quantize (palette = pal, dither = Image.Dither.NONE))
fp[0].save (path ('apng_palette.png'), save_all = True, append_images = fp[1:], duration = 80, loop = 0)

# GIF: delays (incl. 10 ms: shown for 100 ms), disposal methods, and a mix of them (restoring the previous frame only
#   shows in a mix)
g = [x.convert ('RGB') for x in f]
g [0].save (path ('gif_basic.gif'), save_all = True, append_images = g [1:], duration = [50, 100, 10, 20, 200, 70], loop = 0)
for disposal in (1, 2):
    gt = []
    for i in range (6):
        im = Image.new ('RGBA', (W, H), (0, 0, 0, 0)); d = ImageDraw.Draw (im)
        if i == 0:
            d.rectangle ([0, 0, W - 1, H - 1], fill = (30, 30, 90, 255))
        d.rectangle ([5 + i * 6, 10 + i * 5, 25 + i * 6, 30 + i * 5], fill = (250, 120, 20, 255))
        gt.append (im)
    gt[0].save (path (f'gif_dispose{disposal}.gif'), save_all = True, append_images = gt[1:], duration = 60, loop = 0,
                disposal = disposal, transparency = 0, optimize = False)
gm = []
for i in range (8):
    im = Image.new ('RGBA', (W, H), (0, 0, 0, 0)); d = ImageDraw.Draw (im)
    if i % 3 == 0:
        d.rectangle ([0, 0, W - 1, H - 1], fill = (30 + i * 10, 30, 90, 255))
    d.rectangle ([5 + i * 5, 10 + i * 6, 25 + i * 5, 30 + i * 6], fill = (250, 120 - i * 10, 20, 255))
    gm.append (im)
gm[0].save (path ('gif_mixed.gif'), save_all = True, append_images = gm[1:], duration = 60, loop = 0,
            disposal = [1, 3, 2, 3, 1, 3, 2, 0], transparency = 0)

# WebP: lossy with delays, lossless with transparency, and still ones
w = []
for i in range (8):
    im = Image.new ('RGBA', (W, H), (20, 40, 60, 255)); d = ImageDraw.Draw (im)
    d.rectangle ([5 + i * 4, 10, 25 + i * 4, 40], fill = (200, i * 30 % 255, 50, 255)); d.ellipse ([30, 50 + i, 50, 70 + i], fill = (10, 200, 100, 255))
    w.append (im)
w [0].save (path ('webp_lossy.webp'), save_all = True, append_images = w [1:], duration = [50, 100, 0, 5, 200, 70, 30, 40], loop = 0, quality = 80)
wa = []
for i in range (6):
    im = Image.new ('RGBA', (W, H), (0, 0, 0, 0)); d = ImageDraw.Draw (im)
    d.rectangle ([5 + i * 6, 10 + i * 5, 25 + i * 6, 30 + i * 5], fill = (250, 120, 20, 180)); wa.append (im)
wa[0].save (path ('webp_alpha.webp'), save_all = True, append_images = wa[1:], duration = 60, loop = 0, lossless = True)
w [0].save (path ('webp_still.webp'),          quality  = 90)
w [0].save (path ('webp_still_lossless.webp'), lossless = True)

# Still images (not animated)
w [0].convert ('RGB').save (path ('still.png'))
w [0].convert ('RGB').save (path ('still.gif'))

# Malformed: cut off halfway, and not an image at all (they must fail, or keep the frames before the cut, and never crash)
for name in ('apng_basic.png', 'gif_mixed.gif', 'webp_lossy.webp'):
    data = open (path (name), 'rb').read ( )
    base, ext = os.path.splitext (name)
    open (path (base + '_truncated' + ext), 'wb').write (data [: len (data) // 2])
open (path ('garbage.png'), 'wb').write (b'\x89PNG\r\n\x1a\n' + bytes (range (256)) * 4)
open (path ('garbage.gif'), 'wb').write (b'GIF89a' + bytes ((i * 37) % 256 for i in range (1024)))
open (path ('garbage.webp'), 'wb').write (b'RIFF\x00\x04\x00\x00WEBPVP8X' + bytes ((i * 53) % 256 for i in range (1024)))
