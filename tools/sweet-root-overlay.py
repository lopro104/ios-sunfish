#!/usr/bin/env python3
"""Apply the captured phone state to a freshly built iOS root (after tools/mkiosroot.sh and tools/sweet-root-tweaks.py).

  sudo tools/sweet-root-overlay.py ROOT [OVERLAY.tar]        (default research/root-overlay/overlay-clean.tar)

ROOT is the mounted image (mount -t hfsplus -o loop,force). The tar comes from the phone over SSH through an allow-list, see
NOTES: /var/jb, the changed system files, disabled.plist, the Wi-Fi route script. No Wi-Fi networks, keychains, app data, SSH
keys or logs, so a root built with it carries nobody's data. It holds Apple's and Procursus's files: keep it out of git.
/fw is not in it: copy the firmware from research/fw-local, and let the modem's per-device data (/fw/rmtfs) come from the
phone's own partitions (never from someone else's).
"""
import os
import shutil
import sys
import tarfile

root = sys.argv[1]
tar = sys.argv[2] if len(sys.argv) > 2 else 'research/root-overlay/overlay-clean.tar'
assert os.path.isdir(os.path.join(root, 'System/Library')), 'not an iOS root: ' + root
with tarfile.open(tar) as t:
    t.extractall(root, numeric_owner=True, filter='fully_trusted')
# moved away, not changed: waits forever on FairPlay, which hung Settings' iPhone Storage and atc
bundle = os.path.join(root, 'System/Library/SyncBundles/AirFair.syncBundle')
if os.path.isdir(bundle):
    shutil.rmtree(bundle)
print('overlay applied:', tar)
