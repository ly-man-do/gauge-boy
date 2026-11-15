#!/usr/bin/env python3
"""Generate source/obd/dtc_desc_data.c: DTC descriptions, generic and per make.

Source (owner decision 2026-11-15, gauge-boy-bxf.7): the GENERIC rows of
Wal33D/dtc-database (MIT License, Copyright (c) 2024 Wal33D (Waleed Judah)),
pinned to one commit so the table can be regenerated exactly:

  commit  04c43d72e7db7197658b6f72fe582c5076d9eee8
  file    data/dtc_codes.db
  sha256  099a4ffd60398112a0540b0bbc93a5929e05e7f4e6d4988ca2a50858af01b743

Two tables:
  generic       GENERIC rows in the SAE-controlled ranges (P0, P2, P34-P3F,
                B0, B3, C0, C3, U0, U3): the same meaning on every car.
  manufacturer  rows of the named makes in the manufacturer ranges (P1,
                P30-P33, B1, B2, C1, C2, U1, U2), added by owner request
                (2026-11-15, gauge-boy-bxf.12). The same code means different
                things on different makes, so each entry carries a bit mask
                of the makes it applies to; identical text is stored once.
Left out: GENERIC rows in manufacturer ranges and maker rows in generic
ranges (the generic text wins), and the dataset's OTHER rows (make unknown).

Run from the repo root (Python 3 standard library only):

  mkdir -p /tmp/dtcdb && curl -sfL -o /tmp/dtcdb/dtc_codes.db \\
    https://raw.githubusercontent.com/Wal33D/dtc-database/04c43d72e7db7197658b6f72fe582c5076d9eee8/data/dtc_codes.db
  python3 -I tools/make_dtc_table.py /tmp/dtcdb/dtc_codes.db
"""

import hashlib
import os
import re
import sqlite3
import sys

PINNED_SHA256 = "099a4ffd60398112a0540b0bbc93a5929e05e7f4e6d4988ca2a50858af01b743"
SOURCE = ("Wal33D/dtc-database commit 04c43d72e7db7197658b6f72fe582c5076d9eee8, "
          "data/dtc_codes.db")
OUT = os.path.join("source", "obd", "dtc_desc_data.c")
MAX_DESC = 128

# Dataset make IDs -> names shown in the app (bit order = this order).
MAKES = [
    ("ACURA", "Acura"), ("AUDI", "Audi"), ("BMW", "BMW"), ("BUICK", "Buick"),
    ("CADILLAC", "Cadillac"), ("CHEVY", "Chevrolet"), ("CHRYSLER", "Chrysler"),
    ("DODGE", "Dodge"), ("FORD", "Ford"), ("GEO", "Geo"), ("GM", "GM"), ("GMC", "GMC"),
    ("HONDA", "Honda"), ("INFINITI", "Infiniti"), ("JAGUAR", "Jaguar"), ("JEEP", "Jeep"),
    ("KIA", "Kia"), ("LEXUS", "Lexus"), ("LINCOLN", "Lincoln"), ("MAZDA", "Mazda"),
    ("MERCEDES", "Mercedes-Benz"), ("MERCURY", "Mercury"), ("MITSUBISHI", "Mitsubishi"),
    ("NISSAN", "Nissan"), ("OLDSMOBILE", "Oldsmobile"), ("PLYMOUTH", "Plymouth"),
    ("PONTIAC", "Pontiac"), ("SATURN", "Saturn"), ("SUBARU", "Subaru"), ("SUZUKI", "Suzuki"),
    ("TOYOTA", "Toyota"), ("VOLKSWAGEN", "Volkswagen"),
]

ASCII_FIXES = {
    "“": '"', "”": '"', "‘": "'", "’": "'",
    "–": "-", "—": "-", "°": " deg ", " ": " ",
}

LETTERS = "PCBU"  # 2-bit system code as stored in the DTC bytes


def generic_range(code):
    letter, d1, d2 = code[0], code[1], code[2]
    if letter == "P":
        return d1 in "02" or (d1 == "3" and d2 in "456789ABCDEF")
    return d1 in "03"  # B0/B3, C0/C3, U0/U3


def key(code):
    """The two DTC bytes as a 16-bit number (P0171 -> 0x0171)."""
    return (LETTERS.index(code[0]) << 14) | int(code[1:], 16)


def clean(text):
    for a, b in ASCII_FIXES.items():
        text = text.replace(a, b)
    text = re.sub(r"\s+", " ", text).strip()
    if any(ord(c) < 32 or ord(c) > 126 for c in text):
        raise ValueError("non-ASCII left in %r" % text)
    return text[:MAX_DESC]


def c_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    digest = hashlib.sha256(open(path, "rb").read()).hexdigest()
    if digest != PINNED_SHA256:
        sys.exit("%s: sha256 %s, expected the pinned %s" % (path, digest, PINNED_SHA256))
    if not os.path.isdir(os.path.dirname(OUT)):
        sys.exit("run from the repo root")

    db = sqlite3.connect("file:%s?mode=ro" % path, uri=True)
    rows = db.execute(
        "SELECT code, manufacturer, description FROM dtc_definitions "
        "WHERE locale = 'en'").fetchall()
    make_bit = {mid: i for i, (mid, _) in enumerate(MAKES)}
    names_in_db = {r[1] for r in rows} - {"GENERIC", "OTHER"}
    if names_in_db - set(make_bit):
        sys.exit("makes in the database but not in MAKES: %s" % sorted(names_in_db - set(make_bit)))

    generic = {}
    maker = {}          # (key, text) -> make mask
    skipped = 0
    for code, make, desc in rows:
        code = code.strip().upper()
        if not re.fullmatch(r"[PCBU][0-9A-F]{4}", code):
            skipped += 1
            continue
        text = clean(desc)
        if not text:
            skipped += 1
            continue
        if make == "GENERIC" and generic_range(code):
            generic[key(code)] = (code, text)
        elif make in make_bit and not generic_range(code):
            k = (key(code), text)
            maker[k] = maker.get(k, 0) | (1 << make_bit[make])
        else:
            skipped += 1

    entries = sorted(generic.items())
    mfr = sorted(maker.items())
    with open(OUT, "w") as f:
        f.write("/* Generated by tools/make_dtc_table.py: do not edit.\n"
                " *\n"
                " * DTC descriptions from %s:\n"
                " * %d generic codes and %d manufacturer-specific entries for %d makes.\n"
                " *\n"
                " * MIT License, Copyright (c) 2024 Wal33D (Waleed Judah); see CREDITS.md.\n"
                " */\n" % (SOURCE, len(entries), len(mfr), len(MAKES)))
        f.write('#include "dtc_desc_data.h"\n\n')
        f.write("const unsigned dtc_desc_count = %du;\n\n" % len(entries))
        f.write("const dtc_desc_t dtc_desc_table[%d] = {\n" % len(entries))
        for k, (code, text) in entries:
            f.write("    { 0x%04X, %s }, /* %s */\n" % (k, c_string(text), code))
        f.write("};\n\n")
        f.write("const unsigned dtc_make_count = %du;\n\n" % len(MAKES))
        f.write("const char *const dtc_make_names[%d] = {\n" % len(MAKES))
        for i, (mid, name) in enumerate(MAKES):
            f.write("    %s, /* bit %d: %s */\n" % (c_string(name), i, mid))
        f.write("};\n\n")
        f.write("const unsigned dtc_mfr_count = %du;\n\n" % len(mfr))
        f.write("const dtc_mfr_desc_t dtc_mfr_table[%d] = {\n" % len(mfr))
        for (k, text), mask in mfr:
            f.write("    { 0x%04X, 0x%09XULL, %s },\n" % (k, mask, c_string(text)))
        f.write("};\n")
    print("wrote %s: %d generic codes, %d manufacturer entries for %d makes (%d rows left out)"
          % (OUT, len(entries), len(mfr), len(MAKES), skipped))


if __name__ == "__main__":
    main()
