#!/usr/bin/env python3
"""Check an audited adbd's imported auth symbols against the built policy exports.

This is a static coverage check, not a substitute for runtime authentication,
UID, SELinux, network isolation, and reconnect tests on the device.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def auth_symbols(readelf, path, defined):
    output = subprocess.check_output(
        [str(readelf), "--dyn-syms", "--wide", str(path)], text=True
    )
    names = set()
    for line in output.splitlines():
        cells = line.split()
        if len(cells) < 8 or not cells[0].endswith(":"):
            continue
        name = cells[7].split("@", 1)[0]
        if name.startswith("adbd_auth_") and (cells[6] != "UND") == defined:
            if not defined or (cells[3] == "FUNC" and cells[5] == "DEFAULT"):
                names.add(name)
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--readelf", type=Path, required=True)
    parser.add_argument("--adbd", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    reference = json.loads(args.reference.read_text())
    daemon_hash = hashlib.sha256(args.adbd.read_bytes()).hexdigest()
    if daemon_hash != reference["adbd_sha256"]:
        raise SystemExit("The adbd is not the audited reference binary")
    required = auth_symbols(args.readelf, args.adbd, defined=False)
    if not required or required != set(reference["adbd_required_auth_symbols"]):
        raise SystemExit("The imported auth API differs from the audited reference")
    missing = required - auth_symbols(args.readelf, args.policy, defined=True)
    report = {
        "adbd_sha256": daemon_hash,
        "policy_sha256": hashlib.sha256(args.policy.read_bytes()).hexdigest(),
        "required_auth_symbols": sorted(required),
        "missing_interposed_symbols": sorted(missing),
        "static_coverage_passed": not missing,
        "device_runtime_verified": False,
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    if missing:
        raise SystemExit("Missing auth exports: " + ", ".join(sorted(missing)))
    print(f"Auth symbol coverage: PASS ({len(required)} required functions)")


if __name__ == "__main__":
    main()
