#!/usr/bin/env python3
"""Package only a signed, verified release variant; signing properties remain outside Git."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SIGNER = "429de1280138eb96207ea61c6c19a36d2a83404e4654ce52d14e1aba43abf980"


def run(*args, capture=False, env=None):
    return subprocess.run([str(a) for a in args], cwd=ROOT, check=True,
                          text=True, capture_output=capture, env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-ref", default="HEAD")
    args = parser.parse_args()
    run("git", "diff", "--exit-code", args.source_ref, "--", "app", "native", "tools/build-native.sh")
    if run("git", "ls-files", "--others", "--exclude-standard", "--", "app", "native", capture=True).stdout:
        raise RuntimeError("Commit app and native sources before packaging")
    environment = os.environ.copy()
    environment.setdefault("TAILADB_SIGNING_PROPERTIES",
                           str(Path.home() / ".android/keystores/tail-adb-signing.properties"))
    if not Path(environment["TAILADB_SIGNING_PROPERTIES"]).is_file():
        raise RuntimeError("Missing private signing properties; see docs/releasing.md")
    sdk = Path(environment.get("ANDROID_HOME", Path.home() / "Library/Android/sdk"))
    tools = sdk / "build-tools/37.0.0"
    run(ROOT / "gradlew", ":app:lintRelease", ":app:assembleRelease", env=environment)
    output = ROOT / "app/build/outputs/apk/release"
    meta = json.loads((output / "output-metadata.json").read_text())
    if meta["variantName"] != "release" or len(meta["elements"]) != 1:
        raise RuntimeError("Expected one release APK")
    element = meta["elements"][0]
    version = element["versionName"]
    if "validation" in version:
        raise RuntimeError("Validation builds are not distributable")
    apk = output / element["outputFile"]
    badging = run(tools / "aapt2", "dump", "badging", apk, capture=True).stdout
    if ("name='net.ryuya_dev.tailadb'" not in badging or "targetSdkVersion:'37'" not in badging
            or "application-debuggable" in badging):
        raise RuntimeError("Wrong application, target SDK, or debuggable APK")
    manifest = run(tools / "aapt2", "dump", "xmltree", apk, "--file", "AndroidManifest.xml", capture=True).stdout
    if any(word in manifest.lower() for word in ["fixture", "testactivity", "probeactivity", "instrumentation"]):
        raise RuntimeError("Test fixture in APK")
    run(tools / "zipalign", "-c", "-P", "16", "4", apk)
    signature = run(tools / "apksigner", "verify", "--verbose", "--print-certs", apk, capture=True).stdout
    if SIGNER not in signature:
        raise RuntimeError("Distribution signer mismatch")
    with zipfile.ZipFile(apk) as archive:
        names = set(archive.namelist())
        for abi in ["arm64-v8a", "x86_64"]:
            for library in ["exec", "policy", "su"]:
                if f"lib/{abi}/libtailadb_{library}.so" not in names:
                    raise RuntimeError("Missing release native backend")
    dist = ROOT / "artifacts/release"
    dist.mkdir(parents=True, exist_ok=True)
    target = dist / f"TailAdb-{version}-release.apk"
    target.write_bytes(apk.read_bytes())
    (dist / f"TailAdb-{version}-signature.txt").write_text(signature)
    source = dist / f"TailAdb-{version}-source.zip"
    run("git", "archive", "--format=zip", f"--prefix=TailAdb-{version}/",
        f"--output={source}", args.source_ref)
    companion = dist / "tailadb_watch.py"
    companion.write_bytes((ROOT / "tools/tailadb_watch.py").read_bytes())
    files = [target, source, companion]
    (dist / f"SHA256SUMS-{version}.txt").write_text("".join(
        hashlib.sha256(p.read_bytes()).hexdigest() + "  " + p.name + "\n" for p in files))
    print("Signed and verified release:", target.name)


if __name__ == "__main__":
    main()
