#!/usr/bin/env python3
"""Manage known Wi-Fi networks in com.apple.wifi.plist.

Resolves wifid log repeating 'GET KNOWN NETWORKS LIST err=2' by populating
the 'List of known networks' cache in com.apple.wifi.plist.

Usage:
  python3 tools/sweet-known-networks.py --status [--host IP]
  python3 tools/sweet-known-networks.py --add-current [--host IP]
  python3 tools/sweet-known-networks.py --verify [--host IP]
"""

import argparse
import datetime
import os
import plistlib
import subprocess
import sys
import tempfile
import time

DEFAULT_HOST = "192.168.7.2"
REMOTE_PLIST = "/private/var/preferences/SystemConfiguration/com.apple.wifi.plist"
REMOTE_BACKUP = "/private/var/preferences/SystemConfiguration/com.apple.wifi.plist.bak"


def run_ssh(host, cmd):
    res = subprocess.run(
        [
            "ssh",
            "-o", "UserKnownHostsFile=/dev/null",
            "-o", "StrictHostKeyChecking=no",
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


def fetch_plist(host):
    res = subprocess.run(
        [
            "ssh",
            "-o", "UserKnownHostsFile=/dev/null",
            "-o", "StrictHostKeyChecking=no",
            f"root@{host}",
            f"cat {REMOTE_PLIST}",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if res.returncode != 0:
        raise RuntimeError(f"Failed to fetch {REMOTE_PLIST}: {res.stderr.decode('utf-8', errors='replace')}")
    return plistlib.loads(res.stdout)


def push_plist(host, plist_dict):
    data = plistlib.dumps(plist_dict, fmt=plistlib.FMT_BINARY)
    with tempfile.NamedTemporaryFile("wb", delete=False) as tf:
        tf.write(data)
        tf_name = tf.name

    try:
        subprocess.run(
            [
                "scp",
                "-o", "UserKnownHostsFile=/dev/null",
                "-o", "StrictHostKeyChecking=no",
                tf_name,
                f"root@{host}:{REMOTE_PLIST}.tmp",
            ],
            check=True,
        )
        run_ssh(host, f"chmod 0644 {REMOTE_PLIST}.tmp && mv {REMOTE_PLIST}.tmp {REMOTE_PLIST}")
    finally:
        if os.path.exists(tf_name):
            os.remove(tf_name)


def status_cmd(args):
    d = fetch_plist(args.host)
    known = d.get("List of known networks", [])
    print(f"Phone {args.host}: 'List of known networks' has {len(known)} entries:")
    for idx, net in enumerate(known):
        ssid = net.get("SSID_STR", "<unknown>")
        bssid = net.get("BSSID", "<unknown>")
        enabled = net.get("enabled", "<unknown>")
        print(f"  [{idx}] SSID: {ssid}, BSSID: {bssid}, enabled: {enabled}")


def add_current_cmd(args):
    d = fetch_plist(args.host)
    # Ensure backup exists on phone
    run_ssh(args.host, f"[ -f {REMOTE_BACKUP} ] || cp -a {REMOTE_PLIST} {REMOTE_BACKUP}")

    known = d.get("List of known networks", [])
    existing_ssids = {n.get("SSID_STR") for n in known}

    target_ssid = "<HOME-WIFI>"
    target_bssid = "00:00:00:00:00:00"

    # Find info from scanned list if present
    scanned = d.get("List of scanned networks with private mac", [])
    scanned_match = None
    for s in scanned:
        if s.get("SSID_STR") == target_ssid:
            scanned_match = s
            break

    now = datetime.datetime.utcnow()

    entry = {
        "SSID_STR": target_ssid,
        "SSID": target_ssid.encode("utf-8"),
        "BSSID": target_bssid,
        "SecurityMode": "WPA2",
        "SecurityType": 8,
        "supportedSecurityTypes": 8,
        "UserDirected": True,
        "AutoJoin": True,
        "enabled": True,
        "hidden": False,
        "networkUsage": 0.0,
        "lastJoined": now,
        "lastJoinedByUserAt": now,
        "lastUpdated": now,
    }

    if target_ssid in existing_ssids:
        print(f"Network '{target_ssid}' is already in 'List of known networks'. Updating.")
        new_known = []
        for n in known:
            if n.get("SSID_STR") == target_ssid:
                n.update(entry)
            new_known.append(n)
        d["List of known networks"] = new_known
    else:
        print(f"Adding '{target_ssid}' to 'List of known networks'...")
        known.append(entry)
        d["List of known networks"] = known

    push_plist(args.host, d)
    print("Updated com.apple.wifi.plist pushed successfully.")

    # Kickstart wifid to reload preferences
    print("Kickstarting com.apple.wifid...")
    try:
        run_ssh(args.host, "launchctl kickstart -k system/com.apple.wifid")
        print("wifid restarted.")
    except Exception as e:
        print(f"Notice: kickstart wifid returned: {e}")

    time.sleep(2)
    verify_cmd(args)


def verify_cmd(args):
    print(f"Checking wifid log on {args.host} for GET KNOWN NETWORKS LIST...")
    cmd = "timeout 12 oslog --info --debug -p wifid"
    res = subprocess.run(
        [
            "ssh",
            "-o", "UserKnownHostsFile=/dev/null",
            "-o", "StrictHostKeyChecking=no",
            f"root@{args.host}",
            cmd,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    lines = res.stdout.splitlines()
    found = [l for l in lines if "GET KNOWN NETWORKS LIST" in l or "known network" in l.lower()]
    if found:
        print(f"Found {len(found)} matching log lines:")
        for l in found[-10:]:
            print(f"  {l}")
    else:
        print("No 'GET KNOWN NETWORKS LIST' lines in 12s capture. (Settings might need to be open to poll actively)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default=DEFAULT_HOST, help="Phone IP (default %(default)s)")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--status", action="store_true", help="Show known networks on phone")
    group.add_argument("--add-current", action="store_true", help="Add <HOME-WIFI> to known networks")
    group.add_argument("--verify", action="store_true", help="Capture wifid logs to verify err status")
    args = parser.parse_args()

    if args.status:
        status_cmd(args)
    elif args.add_current:
        add_current_cmd(args)
    elif args.verify:
        verify_cmd(args)


if __name__ == "__main__":
    main()
