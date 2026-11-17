import numpy as np
from PIL import Image
from scipy import ndimage

def cut(im_rgb, box):
    x0, y0, x1, y1 = box
    c = im_rgb.crop((x0, y0, x1, y1))
    a = np.asarray(c).astype(int)
    mx, mn = a.max(2), a.min(2)
    c_mx, c_mn = mx, mn
    light = (mn > 150) & ((mx - mn) < 40)
    lab, n = ndimage.label(light)
    edge = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
    bg = np.isin(lab, list(edge))
    fg = ~bg
    fg = ndimage.binary_opening(fg, iterations=1)
    d = ndimage.binary_dilation(fg, iterations=3)
    l2, n2 = ndimage.label(d)
    if n2 == 0:
        return None
    sizes = ndimage.sum(fg, l2, range(1, n2 + 1))
    keep = l2 == (1 + int(np.argmax(sizes)))
    fg = ndimage.binary_fill_holes(fg & keep)
    # drop the floor shadow: long near-black runs in the bottom of the figure
    ys, xs = np.where(fg)
    top, bot = ys.min(), ys.max()
    dark = (mx < 48) & fg
    sh = ndimage.binary_opening(dark, structure=np.ones((1, 21)))
    sh[: int(top + (bot - top) * 0.86)] = False
    sh = ndimage.binary_dilation(sh, structure=np.ones((3, 3)))
    fg2 = fg & ~sh
    lt = fg2 & (c_mn > 150) & ((c_mx - c_mn) < 40)
    ll, nl = ndimage.label(lt)
    shd = ndimage.binary_dilation(sh, iterations=3)
    for k in range(1, nl + 1):
        m = ll == k
        yy = np.where(m)[0]
        if (m & shd).any() or (m.sum() > 40 and yy.max() >= bot - 1):
            fg2 &= ~m
    pw = fg2 & (c_mn > 222) & ((c_mx - c_mn) < 16)
    pl, npl = ndimage.label(pw)
    for k in range(1, npl + 1):
        m = pl == k
        if m.sum() > 25:
            fg2 &= ~m
    fg2 = ndimage.binary_opening(fg2, iterations=1)
    l3, n3 = ndimage.label(ndimage.binary_dilation(fg2, iterations=2))
    sizes = ndimage.sum(fg2, l3, range(1, n3 + 1))
    fg2 = fg2 & (l3 == 1 + int(np.argmax(sizes)))
    rgba = np.dstack([np.asarray(c), (fg2 * 255).astype(np.uint8)])
    img = Image.fromarray(rgba, "RGBA")
    return img.crop(img.getbbox())

def five(path):
    im = Image.open(path).convert("RGB")
    xs = [36, 306, 576, 846, 1106, 1376]
    return [cut(im, (xs[i] + 4, 66, xs[i + 1] - 4, 352)) for i in range(5)]

def penguin(path):
    im = Image.open(path).convert("RGB")
    top = [36, 305, 568, 833, 1103]
    bot = [36, 305, 575, 840, 1105, 1375]
    t = [cut(im, (top[i] + 4, 66, top[i + 1] - 4, 340)) for i in range(4)]       # frames 2..5
    b = [cut(im, (bot[i] + 4, 386, bot[i + 1] - 4, 650)) for i in range(5)]       # 6,6,7,8,9
    return t, b
