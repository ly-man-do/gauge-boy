#!/usr/bin/env python3
"""Check gauge-boy.cia: title ID, product code and version match the Makefile
and source/version.h. Run by CI after `make cia`; also handy locally:

  python3 tools/check_cia.py gauge-boy.cia 0004000003DEDF00 CTR-P-GBOY 0.0.0
"""

import struct
import sys

SIG_SIZES = {  # signature type -> signature + padding size
    0x010003: 0x200 + 0x3C,  # RSA-4096 SHA-256
    0x010004: 0x100 + 0x3C,  # RSA-2048 SHA-256
    0x010005: 0x3C + 0x40,   # ECDSA SHA-256
}


def align64(n):
    return (n + 63) & ~63


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    path, want_tid, want_code, want_version = sys.argv[1:]
    data = open(path, "rb").read()

    hdr_size, _type, _ver, cert_size, tik_size, tmd_size, _meta, _content = \
        struct.unpack_from("<IHHIIIIQ", data, 0)
    tmd_off = align64(align64(align64(hdr_size) + cert_size) + tik_size)
    content_off = align64(tmd_off + tmd_size)

    sig_type = struct.unpack_from(">I", data, tmd_off)[0]
    tmd = tmd_off + 4 + SIG_SIZES[sig_type]
    tid = struct.unpack_from(">Q", data, tmd + 0x4C)[0]
    tver = struct.unpack_from(">H", data, tmd + 0x9C)[0]

    ncch = content_off
    if data[ncch + 0x100:ncch + 0x104] != b"NCCH":
        sys.exit("first content is not an NCCH")
    program_id = struct.unpack_from("<Q", data, ncch + 0x118)[0]
    code = data[ncch + 0x150:ncch + 0x160].rstrip(b"\0").decode("ascii")

    version = "%d.%d.%d" % (tver >> 10, (tver >> 4) & 0x3F, tver & 0xF)
    print("title ID     %016X" % tid)
    print("program ID   %016X" % program_id)
    print("product code %s" % code)
    print("version      %s" % version)

    errors = []
    if "%016X" % tid != want_tid.upper():
        errors.append("TMD title ID is %016X, want %s" % (tid, want_tid))
    if "%016X" % program_id != want_tid.upper():
        errors.append("NCCH program ID is %016X, want %s" % (program_id, want_tid))
    if code != want_code:
        errors.append("product code is %s, want %s" % (code, want_code))
    if version != want_version:
        errors.append("version is %s, want %s" % (version, want_version))
    for e in errors:
        print("error: " + e, file=sys.stderr)
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
