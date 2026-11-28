import glob, os, sys
from PIL import Image
d = sys.argv[1]
for p in sorted(glob.glob(os.path.join(d, "*.ppm"))):
    im = Image.open(p)
    im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(p[:-4] + ".png")
    os.remove(p)
print("screenshots in", d)
