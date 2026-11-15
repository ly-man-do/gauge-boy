#!/usr/bin/env python3
"""Make the QR codes for installing GaugeBoy with FBI (Remote Install >
Scan QR Code).

  python3 tools/make_qr.py
      The stable QR for the README: always the newest release,
      https://github.com/<repo>/releases/latest/download/gaugeboy.cia
      -> docs/qr-latest.png

  python3 tools/make_qr.py --version 0.0.2 --out gaugeboy-qr.png
      One release's QR (the release workflow attaches it to the release):
      https://github.com/<repo>/releases/download/v0.0.2/gaugeboy.cia

Needs segno (dev / CI only, never shipped):
  pip install --require-hashes -r tools/requirements-release.txt
"""

import argparse
import os
import re
import sys

DEFAULT_REPO = "ly-man-do/gauge-boy"
ASSET = "gaugeboy.cia"


def install_url(repo, version):
    if version is None:
        return "https://github.com/%s/releases/latest/download/%s" % (repo, ASSET)
    return "https://github.com/%s/releases/download/v%s/%s" % (repo, version, ASSET)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--version", help="release version, e.g. 0.0.2 (default: latest release)")
    ap.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", DEFAULT_REPO),
                    help="GitHub owner/name (default: %(default)s)")
    ap.add_argument("--out", help="PNG to write (default: docs/qr-latest.png for the stable QR)")
    ap.add_argument("--scale", type=int, default=8, help="pixels per module (default: 8)")
    args = ap.parse_args()

    if args.version is not None and not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        sys.exit("--version must look like 1.2.3")
    if not re.fullmatch(r"[\w.-]+/[\w.-]+", args.repo):
        sys.exit("--repo must look like owner/name")
    out = args.out or ("docs/qr-latest.png" if args.version is None else "gaugeboy-qr.png")

    try:
        import segno
    except ImportError:
        sys.exit("segno is missing: pip install --require-hashes -r tools/requirements-release.txt")

    url = install_url(args.repo, args.version)
    # Level M: survives a little glare or a scuffed screen; the URL still
    # fits a small symbol that the 3DS camera reads easily.
    qr = segno.make_qr(url, error="m", boost_error=False)
    qr.save(out, kind="png", scale=args.scale, border=4, dark="#000000", light="#ffffff")
    print("wrote %s (QR version %s, %s) for %s" % (out, qr.version, qr.error, url))


if __name__ == "__main__":
    main()
