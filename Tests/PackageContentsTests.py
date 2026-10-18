#!/usr/bin/env python3
"""Inspect a built PKG without installing it or running its privileged payload."""
import plistlib
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

package = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parent.parent
failures = []

def check(condition, description):
    print(('PASS ' if condition else 'FAIL ') + description)
    if not condition:
        failures.append(description)

listing = subprocess.check_output(['pkgutil', '--payload-files', str(package)], text=True)
check(not any('/._' in line for line in listing.splitlines()), 'payload excludes AppleDouble metadata entries')
with tempfile.TemporaryDirectory(prefix='wired-hotspot-package-test-') as temporary:
    expanded = Path(temporary) / 'expanded'
    subprocess.run(['pkgutil', '--expand-full', str(package), str(expanded)], check=True)
    payload = expanded / 'Payload'
    app = payload / 'Applications/HoRNDIS Status.app'
    info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
    raw = Path(temporary) / 'raw'
    subprocess.run(['pkgutil', '--expand', str(package), str(raw)], check=True)
    archive_paths = subprocess.check_output(['tar', '-tf', str(raw / 'Payload')], text=True).splitlines()
    check(not any('/._' in line for line in archive_paths), 'actual archive excludes AppleDouble entries')
    archive_metadata = subprocess.check_output(['tar', '--numeric-owner', '-tvf', str(raw / 'Payload')], text=True).splitlines()
    check(all(re.match(r'\S+\s+\d+\s+0\s+0\s+', line) for line in archive_metadata),
          'actual archive keeps root/wheel ownership')
    package_info = ET.parse(expanded / 'PackageInfo').getroot()
    version = package_info.get('version')
    bom = subprocess.check_output(['lsbom', str(expanded / 'Bom')], text=True).splitlines()
    check(set(archive_paths) == {line.split('\t')[0] for line in bom}, 'archive inventory matches BOM')
    check(int(package_info.find('payload').get('numberOfFiles')) == len(bom), 'payload file count matches BOM')
    check(all(line.split('\t')[2] == '0/0' for line in bom), 'BOM keeps root/wheel ownership')
    check(package_info.find('bundle-version/bundle') is None, 'independent versions replace an existing upstream app')
    make_version = re.search(r'^VERSION\s*\?=\s*(\S+)', (root / 'Makefile').read_text(), re.M).group(1)
    check(version == info['CFBundleShortVersionString'] == make_version, 'package, app and main version agree')
    check(info['CFBundleDevelopmentRegion'] == 'zh-Hans', 'Chinese development language is packaged')
    cli = payload / 'usr/local/bin/horndis'
    embedded = app / 'Contents/Resources/horndis'
    check(cli.read_bytes() == embedded.read_bytes(), 'app helper matches installed CLI')
    for binary in [cli, embedded, app / 'Contents/MacOS/horndis-status']:
        check(str(root).encode() not in binary.read_bytes(), 'binary excludes local workspace path: ' + binary.name)
        archs = subprocess.check_output(['lipo', '-archs', str(binary)], text=True).split()
        check({'arm64', 'x86_64'} <= set(archs), 'binary is universal: ' + binary.name)
    for target in [cli, app]:
        result = subprocess.run(['codesign', '--verify', '--deep', '--strict', str(target)], capture_output=True)
        check(result.returncode == 0, 'signature verifies: ' + target.name)
raise SystemExit(bool(failures))
