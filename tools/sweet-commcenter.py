#!/usr/bin/env python3
"""Manage com.apple.CommCenter jobs in /System/Library/xpc/launchd.plist.

Can operate directly on the connected phone via SSH, or on a local launchd.plist.
Ensures backups are made and provides an easy undo mechanism.

Usage:
  python3 tools/sweet-commcenter.py --status [--host IP]
  python3 tools/sweet-commcenter.py --enable [--host IP]
  python3 tools/sweet-commcenter.py --disable [--host IP]
  python3 tools/sweet-commcenter.py --file PATH --enable|--disable
"""

import argparse
import os
import plistlib
import subprocess
import sys

DEFAULT_HOST = "192.168.7.2"
REMOTE_LAUNCHD = "/System/Library/xpc/launchd.plist"
REMOTE_BACKUP = "/var/root/launchd.plist.before-commcenter"
REMOTE_CC_PLIST = "/System/Library/LaunchDaemons/com.apple.CommCenter.plist"
REMOTE_CC_NONCELL_PLIST = "/System/Library/LaunchDaemons/com.apple.CommCenter-noncellular.plist"

COMMCENTER_KEYS = [
    "/System/Library/LaunchDaemons/com.apple.CommCenter.plist",
    "/System/Library/LaunchDaemons/com.apple.CommCenter-noncellular.plist",
]


def run_ssh(host, cmd):
    res = subprocess.run(
        [
            "ssh",
            "-o",
            "UserKnownHostsFile=/dev/null",
            "-o",
            "StrictHostKeyChecking=no",
            f"root@{host}",
            cmd,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if res.returncode != 0:
        raise RuntimeError(f"SSH command failed ({res.returncode}): {res.stderr.strip()}")
    return res.stdout


def fetch_file(host, remote_path):
    res = subprocess.run(
        [
            "ssh",
            "-o",
            "UserKnownHostsFile=/dev/null",
            "-o",
            "StrictHostKeyChecking=no",
            f"root@{host}",
            f"cat {remote_path}",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if res.returncode != 0:
        raise RuntimeError(f"Failed to fetch {remote_path}: {res.stderr.decode('utf-8', errors='replace')}")
    return res.stdout


def push_file(host, local_path, remote_path, mode="0644"):
    subprocess.run(
        [
            "scp",
            "-o",
            "UserKnownHostsFile=/dev/null",
            "-o",
            "StrictHostKeyChecking=no",
            local_path,
            f"root@{host}:{remote_path}.tmp",
        ],
        check=True,
    )
    run_ssh(host, f"chmod {mode} {remote_path}.tmp && mv {remote_path}.tmp {remote_path}")


def get_commcenter_dicts(host=None, root=None):
    if root:
        cc_p = os.path.join(root, "System/Library/LaunchDaemons/com.apple.CommCenter.plist")
        cc_non_p = os.path.join(root, "System/Library/LaunchDaemons/com.apple.CommCenter-noncellular.plist")
        with open(cc_p, "rb") as f:
            cc = plistlib.load(f)
        with open(cc_non_p, "rb") as f:
            cc_non = plistlib.load(f)
        return cc, cc_non
    elif host:
        cc_bytes = fetch_file(host, REMOTE_CC_PLIST)
        cc_non_bytes = fetch_file(host, REMOTE_CC_NONCELL_PLIST)
        return plistlib.loads(cc_bytes), plistlib.loads(cc_non_bytes)
    else:
        # Fallback to local files if present
        local_cc = "scratch/com.apple.CommCenter.plist"
        local_cc_non = "scratch/com.apple.CommCenter-noncellular.plist"
        with open(local_cc, "rb") as f:
            cc = plistlib.load(f)
        with open(local_cc_non, "rb") as f:
            cc_non = plistlib.load(f)
        return cc, cc_non


def check_status(data):
    daemons = data.get("LaunchDaemons", {})
    present = [k for k in COMMCENTER_KEYS if k in daemons]
    labels = [daemons[k].get("Label") for k in present]
    return present, labels


def enable_commcenter(data, cc, cc_non):
    daemons = data.setdefault("LaunchDaemons", {})
    daemons[COMMCENTER_KEYS[0]] = cc
    daemons[COMMCENTER_KEYS[1]] = cc_non
    return data


def disable_commcenter(data):
    daemons = data.get("LaunchDaemons", {})
    removed = 0
    for k in list(daemons.keys()):
        if k in COMMCENTER_KEYS or daemons[k].get("Label") == "com.apple.CommCenter":
            del daemons[k]
            removed += 1
    return data, removed


def main():
    parser = argparse.ArgumentParser(description="Enable/disable CommCenter in launchd.plist")
    parser.add_argument("--host", default=DEFAULT_HOST, help="Phone IP address")
    parser.add_argument("--file", help="Path to local launchd.plist instead of remote phone")
    parser.add_argument("--status", action="store_true", help="Check CommCenter presence")
    parser.add_argument("--enable", action="store_true", help="Re-add CommCenter jobs")
    parser.add_argument("--disable", action="store_true", help="Remove CommCenter jobs (or restore backup)")
    args = parser.parse_args()

    if args.file:
        with open(args.file, "rb") as f:
            data = plistlib.load(f)

        present, labels = check_status(data)
        if args.status:
            print(f"CommCenter jobs present: {len(present)}/{len(COMMCENTER_KEYS)}: {present}")
            return

        if args.enable:
            cc, cc_non = get_commcenter_dicts()
            data = enable_commcenter(data, cc, cc_non)
            with open(args.file, "wb") as f:
                plistlib.dump(data, f, fmt=plistlib.FMT_BINARY)
            print(f"Enabled CommCenter in {args.file}")

        elif args.disable:
            data, removed = disable_commcenter(data)
            with open(args.file, "wb") as f:
                plistlib.dump(data, f, fmt=plistlib.FMT_BINARY)
            print(f"Disabled CommCenter in {args.file} ({removed} jobs removed)")
        return

    # Operating on remote phone
    host = args.host
    print(f"Checking phone at {host}...")
    raw = fetch_file(host, REMOTE_LAUNCHD)
    data = plistlib.loads(raw)
    present, labels = check_status(data)

    if args.status:
        print(f"Phone {host}: CommCenter jobs present: {len(present)}/{len(COMMCENTER_KEYS)}: {present}")
        return

    if args.enable:
        if len(present) == len(COMMCENTER_KEYS):
            print("CommCenter is already enabled in launchd.plist on the phone.")
            return

        print(f"Backing up current {REMOTE_LAUNCHD} to {REMOTE_BACKUP} on phone...")
        run_ssh(host, f"cp -p {REMOTE_LAUNCHD} {REMOTE_BACKUP}")

        cc, cc_non = get_commcenter_dicts(host=host)
        data = enable_commcenter(data, cc, cc_non)

        tmp_local = "scratch/launchd.plist.with-commcenter"
        os.makedirs("scratch", exist_ok=True)
        with open(tmp_local, "wb") as f:
            plistlib.dump(data, f, fmt=plistlib.FMT_BINARY)

        print(f"Pushing updated launchd.plist to phone {REMOTE_LAUNCHD}...")
        push_file(host, tmp_local, REMOTE_LAUNCHD, mode="0644")
        print("Done. CommCenter enabled. Backup is at /var/root/launchd.plist.before-commcenter.")

    elif args.disable:
        print(f"Checking if backup {REMOTE_BACKUP} exists on phone...")
        try:
            run_ssh(host, f"test -f {REMOTE_BACKUP}")
            print(f"Restoring {REMOTE_LAUNCHD} from {REMOTE_BACKUP}...")
            run_ssh(host, f"cp -p {REMOTE_BACKUP} {REMOTE_LAUNCHD}")
            print("Done. Restored from backup.")
        except RuntimeError:
            print("No backup found, stripping CommCenter jobs directly...")
            data, removed = disable_commcenter(data)
            tmp_local = "scratch/launchd.plist.no-commcenter"
            os.makedirs("scratch", exist_ok=True)
            with open(tmp_local, "wb") as f:
                plistlib.dump(data, f, fmt=plistlib.FMT_BINARY)
            push_file(host, tmp_local, REMOTE_LAUNCHD, mode="0644")
            print(f"Done. Removed {removed} CommCenter jobs.")


if __name__ == "__main__":
    main()
