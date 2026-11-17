#!/usr/bin/env python3
"""Make Gauge Boy's offline VIN model-name table from NHTSA vPIC (dev only).

vPIC (https://vpic.nhtsa.dot.gov/) is US government data (public domain,
17 USC 105). Gauge Boy uses it for model names only, by owner decision
(brief 1.15.0, gauge-boy-1hi). The source is the pinned "lite" standalone
database as plain PostgreSQL SQL; this script reads its COPY blocks
directly, no database needed:

    tools/make_vin_models.py vPICList_lite_2026_11.plain.zip \\
        > source/obd/vin_model_data.c

How vPIC finds a model (vpic.spvindecode_core, element 28 "Model"):
  - the WMI is VIN positions 1-3 (plus 12-14 when position 3 is '9');
  - the keys are positions 4-8, '|', positions 10-17;
  - a pattern row matches when its keys match the start of those keys,
    '*' matching any one character and '[...]' a set of characters;
  - only schemas linked to the WMI for the model year count;
  - of several matches, the first by schema start year (newest), then
    the pattern's last change (newest), then fewer fixed characters,
    then the keys, then the row id.
The table keeps the rows in that order, so the first match wins.

Kept: passenger cars, MPVs and trucks (the OBD-II vehicles), WMIs public
by the release date, schemas not waiting for QC, model years 1996 and
later. Models only: no trim, series or engine.
"""

import io
import re
import sys
import zipfile
from collections import defaultdict

FIRST_YEAR = 1996
LAST_YEAR = 2999
MODEL_ELEMENT = 28
VEHICLE_TYPES = {2, 3, 7}   # passenger car, truck, MPV
RELEASE = "vPICList_lite_2026_11"
RELEASE_DATE = "2026-11-14"   # public availability cut-off (release date)
# https://vpic.nhtsa.dot.gov/downloads/vPICList_lite_2026_11.plain.zip
RELEASE_ZIP_SHA256 = "0612f95967598d8379fd9b31bc3e7e81d853d03dabf6299c259484a27dd6a5c8"

TABLES = {
    "wmi": None, "wmi_vinschema": None, "vinschema": None, "pattern": None, "model": None,
    "make": None, "make_model": None,
}

# vPIC make names are upper case; short ones stay so (BMW, GMC, RAM).
MAKE_CASE = {"KIA": "Kia", "MINI": "MINI", "FIAT": "FIAT", "SAAB": "Saab", "AUDI": "Audi",
             "JEEP": "Jeep", "FORD": "Ford", "MAZDA": "Mazda", "SCION": "Scion"}


def make_case(name):
    if name in MAKE_CASE:
        return MAKE_CASE[name]
    def word(w):
        return w if len(w) <= 3 else w[:1] + w[1:].lower()
    return " ".join("-".join(word(p) for p in part.split("-")) for part in name.split(" "))


def open_sql(path):
    if path.endswith(".zip"):
        import hashlib
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        if h.hexdigest() != RELEASE_ZIP_SHA256:
            sys.exit("%s is not the pinned vPIC release %s (sha256 %s)"
                     % (path, RELEASE, h.hexdigest()))
        z = zipfile.ZipFile(path)
        name = [n for n in z.namelist() if n.endswith(".sql")][0]
        return io.TextIOWrapper(z.open(name), encoding="utf-8", newline="\n")
    return open(path, encoding="utf-8", newline="\n")


def read_tables(path):
    """{table: (columns, rows)} for the tables we need; pattern rows are
    filtered to the Model element while reading (the table is large)."""
    out = {}
    cur = None
    cols = None
    rows = None
    for line in open_sql(path):
        if cur is None:
            m = re.match(r"^COPY vpic\.(\w+) \(([^)]*)\) FROM stdin;", line)
            if m and m.group(1) in TABLES:
                cur = m.group(1)
                cols = [c.strip() for c in m.group(2).split(",")]
                rows = []
            continue
        if line.startswith("\\."):
            out[cur] = (cols, rows)
            cur = None
            continue
        f = line.rstrip("\n").split("\t")
        if cur == "pattern" and f[cols.index("elementid")] != str(MODEL_ELEMENT):
            continue
        rows.append(dict(zip(cols, f)))
    missing = set(TABLES) - set(out)
    if missing:
        sys.exit("missing tables: %s" % ", ".join(sorted(missing)))
    return out


def null(v):
    return None if v == "\\N" else v


def fixed_chars(keys):
    return len(keys.replace("*", ""))


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    t = read_tables(sys.argv[1])

    models = {r["id"]: r["name"].strip() for r in t["model"][1]}
    makes = {r["id"]: make_case(r["name"].strip().upper()) for r in t["make"][1]}
    model_make = {}
    for r in t["make_model"][1]:
        model_make.setdefault(r["modelid"], makes.get(r["makeid"], ""))
    qc = {r["id"] for r in t["vinschema"][1] if r["tobeqced"] == "t"}
    wmis = {}
    for r in t["wmi"][1]:
        avail = null(r["publicavailabilitydate"])
        vt = null(r["vehicletypeid"])
        if avail is None or avail[:10] > RELEASE_DATE or vt is None or int(vt) not in VEHICLE_TYPES:
            continue
        wmis[r["id"]] = r["wmi"].upper()
    links = defaultdict(list)   # vinschema id -> [(wmi, from, to)]
    for r in t["wmi_vinschema"][1]:
        if r["wmiid"] not in wmis or r["vinschemaid"] in qc:
            continue
        y0 = int(r["yearfrom"])
        y1 = int(null(r["yearto"]) or LAST_YEAR)
        if y1 < FIRST_YEAR:
            continue
        links[r["vinschemaid"]].append((wmis[r["wmiid"]], y0, y1))

    rules = defaultdict(list)   # wmi -> [(sort key, from, to, keys, model)]
    for r in t["pattern"][1]:
        model = models.get(r["attributeid"])
        if not model or "#" in r["keys"]:
            continue
        name = (model_make.get(r["attributeid"], ""), model)
        keys = r["keys"].upper()
        changed = null(r["updatedon"]) or null(r["createdon"]) or ""
        for wmi, y0, y1 in links.get(r["vinschemaid"], ()):
            order = (-y0, _neg(changed), fixed_chars(keys),
                     keys.replace("[", "").replace("]", ""), int(r["id"]))
            rules[wmi].append((order, max(y0, FIRST_YEAR), y1, keys, name))

    names = sorted({x[4] for rs in rules.values() for x in rs})
    name_ix = {n: i for i, n in enumerate(names)}
    keyset = sorted({x[3] for rs in rules.values() for x in rs})
    key_ix = {k: i for i, k in enumerate(keyset)}

    w = sys.stdout.write
    w("/* Generated by tools/make_vin_models.py from NHTSA vPIC %s\n" % RELEASE)
    w(" * (https://vpic.nhtsa.dot.gov/downloads/, US government data, public\n")
    w(" * domain). Do not edit. Model names only, model years %d and later,\n" % FIRST_YEAR)
    w(" * passenger cars, MPVs and trucks. */\n\n")
    w('#include "vin_model_data.h"\n\n')
    w("const vin_model_name_t vin_model_names[] = {\n")
    for make, model in names:
        w("    { %s, %s },\n" % (c_string(make), c_string(model)))
    w("};\n\n")
    w("const char *const vin_model_keys[] = {\n")
    for k in keyset:
        w("    %s,\n" % c_string(k))
    w("};\n\n")
    w("const vin_model_rule_t vin_model_rules[] = {\n")
    wmi_rows = []
    count = 0
    for wmi in sorted(rules):
        rs = sorted(rules[wmi])
        seen = set()
        first = count
        for order, y0, y1, keys, name in rs:
            item = (y0, y1, keys, name)
            if item in seen:
                continue
            seen.add(item)
            w("    { %d, %d, %d, %d },\n" % (y0 - FIRST_YEAR, min(y1, FIRST_YEAR + 255) - FIRST_YEAR,
                                          key_ix[keys], name_ix[name]))
            count += 1
        wmi_rows.append((wmi, first, count - first))
    w("};\n\n")
    w("const vin_model_wmi_t vin_model_wmis[] = {\n")
    for wmi, first, n in wmi_rows:
        w("    { %s, %d, %d },\n" % (c_string(wmi), first, n))
    w("};\n\n")
    w("const int vin_model_name_count = %d;\n" % len(names))
    w("const int vin_model_key_count = %d;\n" % len(keyset))
    w("const int vin_model_rule_count = %d;\n" % count)
    w("const int vin_model_wmi_count = %d;\n" % len(wmi_rows))
    sys.stderr.write("%d WMIs, %d rules, %d patterns, %d model names\n"
                     % (len(wmi_rows), count, len(keyset), len(names)))


def _neg(s):
    """Sort newest first by a timestamp string."""
    return tuple(-ord(c) for c in s)


if __name__ == "__main__":
    main()
