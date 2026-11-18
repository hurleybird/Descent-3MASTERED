"""Check six RGB game captures, supplied as three matched (normal, control) pairs.

Use the same 1280x720 cockpit, seed 12345 and timestep 1/60, per-pixel lighting
and dynamic shadows on, SSAA1/MSAA0/AO/bloom off. Add -capture-model-casters-only
only to the second capture in each pair. No comparison uses a golden JPEG.
"""
import argparse
from PIL import Image, ImageChops


def counts(paths, box, threshold):
    crops = []
    for path in paths:
        with Image.open(path) as image:
            if image.size != (1280, 720):
                raise ValueError(f"{path}: expected 1280x720, got {image.size}")
            crops.append(image.convert("RGB").crop(box))
    # Only darkening from room casters, measured in any of the three channels.
    channels = ImageChops.subtract(crops[1], crops[0]).split()
    peak = ImageChops.lighter(ImageChops.lighter(*channels[:2]), channels[2])
    return sum(peak.histogram()[threshold+1:])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name, help_text in (
        ("spike", "save 005, frame 50, headlight on"),
        ("flare", "save 005, frame 43, headlight off, fire flare"),
        ("tunnel", "save 004, frame 50, headlight on"),
    ):
        parser.add_argument("--" + name, required=True, nargs=2, metavar=("FULL", "MODELS"), help=help_text)
    args = parser.parse_args()
    checks = [
        ("spike shadow on rock wall", counts(args.spike, (325, 180, 785, 410), 32), 2500, 30000),
        ("stipple on flare host", counts(args.flare, (650, 205, 745, 330), 12), 0, 180),
        ("rock occludes flare", counts(args.flare, (464, 100, 543, 275), 32), 5000, 13825),
        ("outlines across tunnel walls", counts(args.tunnel, (0, 80, 1280, 450), 12), 0, 600),
    ]
    for name, result, lo, hi in checks:
        print(f"{name}: {result} pixels (expected {lo}..{hi})")
    raise SystemExit(0 if all(lo <= result <= hi for _, result, lo, hi in checks) else 1)
