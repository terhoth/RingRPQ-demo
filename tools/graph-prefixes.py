#!/usr/bin/env python3
"""Derive the "prefixes" map for a graph's graph_doc.json entry.

Reads the @prefix header of the source Turtle file and keeps only those
namespaces that actually occur in the built index's .P (predicate) and .SO
(node) dictionaries — YAGO's header declares 34 prefixes, of which the index
uses 6, and offering the other 28 in the UI would just invite queries that
cannot match anything.

    tools/graph-prefixes.py Ring-RPQ/data/yago-facts.ttl Ring-RPQ/data/yago.dat

Prints a JSON object to stdout, ready to paste as the "prefixes" value of that
graph's entry in <data dir>/graph_doc.json.
"""

import argparse
import json
import re
import sys

PREFIX_LINE = re.compile(r'^@prefix\s+([^:]*):\s*<([^>]*)>\s*\.')
DICT_LINE = re.compile(r'^\d+\s+<(.*)>\s*$')


def read_declared_prefixes(ttl_path):
    """{prefix: namespace} from the leading @prefix block of a Turtle file."""
    declared = {}
    with open(ttl_path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if not line.startswith("@"):
                break  # the header ends at the first triple
            match = PREFIX_LINE.match(line)
            if match:
                declared[match.group(1)] = match.group(2)
    return declared


def read_used_namespaces(dict_path):
    """The set of namespaces (URI up to and including the last '/' or '#')
    appearing in one dictionary file."""
    used = set()
    with open(dict_path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = DICT_LINE.match(line)
            if not match:
                continue
            uri = match.group(1)
            cut = max(uri.rfind("/"), uri.rfind("#"))
            if cut != -1:
                used.add(uri[: cut + 1])
    return used


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("ttl", help="source Turtle file, for its @prefix header")
    parser.add_argument("dat", help="built index base path, e.g. .../yago.dat")
    args = parser.parse_args()

    declared = read_declared_prefixes(args.ttl)
    if not declared:
        sys.exit(f"no @prefix declarations found in {args.ttl}")

    used = set()
    for suffix in (".P", ".SO"):
        used |= read_used_namespaces(args.dat + suffix)

    kept = {p: ns for p, ns in sorted(declared.items()) if ns in used}
    unused = sorted(set(declared) - set(kept))

    print(json.dumps(kept, indent=4))
    print(f"\n{len(kept)} of {len(declared)} declared prefixes occur in the index; "
          f"dropped: {', '.join(unused)}", file=sys.stderr)


if __name__ == "__main__":
    main()
