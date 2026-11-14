#!/usr/bin/env python3
"""Print the CHANGELOG.md section for one version, for the GitHub release body.

  python3 tools/release_notes.py 0.0.1

Matches a Keep a Changelog heading such as "## [0.0.1] - 2026-11-15" and prints
everything up to the next "## [" heading. Exits 1 if the section is missing or
empty, so a release can't go out without changelog notes.
"""

import re
import sys


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    version = sys.argv[1]
    heading = re.compile(r"^## \[" + re.escape(version) + r"\]")

    lines = open("CHANGELOG.md", encoding="utf-8").read().splitlines()
    out = None
    for line in lines:
        if out is None:
            if heading.match(line):
                out = []
        elif line.startswith("## ["):
            break
        else:
            out.append(line)

    body = "\n".join(out or []).strip()
    if not body:
        sys.exit("CHANGELOG.md has no notes under '## [%s]'. Move the "
                 "Unreleased entries into a section for this version first." % version)
    print(body)


if __name__ == "__main__":
    main()
