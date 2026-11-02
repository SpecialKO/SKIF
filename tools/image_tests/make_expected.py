# Makes expected.txt of tools/image_tests: what each test image should decode to, from reference decoders (ffmpeg for
#   APNG and GIF, Pillow for WebP), so the tests do not compare SKIF's decoders with themselves. Needs ffmpeg in the
#     path and Pillow (pip install pillow). Only needed when the images change: expected.txt is part of the repository.
#
#   python tools/image_tests/make_expected.py
#
# expected.txt: a line per image, <file> <frames> <width> <height>, then a line per frame with its delay in ms (delays
#   of 10 ms or less are shown for 100 ms, as browsers do). frames/<file>.z: its frames, one after the other as RGBA,
#     zlib-compressed (image_tests decompresses them with stb_image's zlib), with the color of fully transparent pixels
#       taken as black (decoders differ there, and it is never seen).

import os, subprocess, zlib
from PIL import Image

HERE   = os.path.dirname (os.path.abspath (__file__))
IMAGES = os.path.join (HERE, 'images')
FRAMES = os.path.join (HERE, 'frames')
os.makedirs (FRAMES, exist_ok = True)

def normalize (rgba):
    out = bytearray (rgba)
    for i in range (0, len (out), 4):
        if out [i + 3] == 0:
            out [i] = out [i + 1] = out [i + 2] = 0
    return bytes (out)

def delays (path):
    im, out = Image.open (path), []
    for i in range (getattr (im, 'n_frames', 1)):
        im.seek (i); im.load ( ) # WebP: the duration is only known once the frame is loaded
        ms = int (round (im.info.get ('duration', 0)))
        out.append (100 if ms <= 10 else ms)
    return out

def frames_ffmpeg (path, fmt, width, height):
    raw = subprocess.run (['ffmpeg', '-v', 'error', '-f', fmt, '-i', path, '-fps_mode', 'passthrough',
                           '-f', 'rawvideo', '-pix_fmt', 'rgba', '-'], capture_output = True, check = True).stdout
    size = width * height * 4
    return [raw [i : i + size] for i in range (0, len (raw), size)]

def frames_pillow (path):
    im, out = Image.open (path), []
    for i in range (im.n_frames):
        im.seek (i)
        out.append (im.convert ('RGBA').tobytes ( ))
    return out

lines = []
for name in sorted (os.listdir (IMAGES)):
    if '_truncated' in name or name.startswith ('garbage') or name.startswith ('still') or 'webp_still' in name:
        continue # Checked by image_tests.cpp itself
    path = os.path.join (IMAGES, name)
    im   = Image.open (path); w, h = im.size
    if   name.endswith ('.png'):  f = frames_ffmpeg (path, 'apng', w, h)
    elif name.endswith ('.gif'):  f = frames_ffmpeg (path, 'gif',  w, h)
    else:                         f = frames_pillow (path)
    d = delays (path)
    if name.endswith ('.png') and im.info.get ('default_image'):
        d = d [1:] # Pillow counts the default image as a frame, ffmpeg (and SKIF) do not
    assert len (d) == len (f), (name, len (d), len (f))
    lines.append (f'{name} {len (f)} {w} {h}')
    lines += [f'  {d [i]}' for i in range (len (f))]
    open (os.path.join (FRAMES, name + '.z'), 'wb').write (zlib.compress (b''.join (normalize (x) for x in f), 9))

open (os.path.join (HERE, 'expected.txt'), 'w', newline = '\r\n').write ('\n'.join (lines) + '\n')
print ('\n'.join (lines))
