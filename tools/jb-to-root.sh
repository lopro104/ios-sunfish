#!/bin/bash
# Put the prefix tools/mk-jb.sh made (jb/root/var/jb: Procursus rootless bootstrap, Sileo,
# ElleKit, OpenSSH) onto a MOUNTED iOS root, read-write:
#
#   tools/jb-to-root.sh MOUNTED_ROOT [PUBLIC_KEY]     (PUBLIC_KEY default ~/.ssh/id_ed25519.pub)
#
# - /private/var/jb owned by root:wheel (the bootstrap's owners; var/jb/var/mobile is mobile's);
# - Sileo.app also in /Applications, where LaunchServices registers it as a system application
#   (as HackiOS's userland/build.sh does with the prefix's applications);
# - root's SSH: a host key made here once (jb/ssh_host_ed25519_key), and PUBLIC_KEY as the only
#   authorized key; sshd allows nothing but that key (no passwords, no PAM);
# - two launchd jobs in /System/Library/xpc/launchd.plist: com.sweet.jbprep runs /var/jb/prep.sh at
#   boot (dpkg's "firmware" package, pwd.db, the shells), com.sweet.sshd keeps sshd running;
#   com.sweet.gputest goes (jetsam kills it at start anyway);
# - LaunchServices' database removed, so lsd registers Sileo on the next boot.
# Then: fsck.hfsplus, and write the image back to userdata.
set -euo pipefail
R=${1:?usage: jb-to-root.sh MOUNTED_ROOT [PUBLIC_KEY]}
KEY=${2:-$HOME/.ssh/id_ed25519.pub}
HERE=$(cd "$(dirname "$0")/.." && pwd)
JB=$HERE/jb/root/var/jb
[ -d "$R/System/Library/xpc" ] || { echo "$R is not an iOS root" >&2; exit 1; }
[ -d "$JB" ] || { echo "no $JB: run tools/mk-jb.sh first" >&2; exit 1; }
[ -f "$KEY" ] || { echo "no public key $KEY" >&2; exit 1; }

sudo rsync -aH --chown=0:0 "$JB/" "$R/private/var/jb/"
sudo chown 501:501 "$R/private/var/jb/var/mobile"
sudo rsync -aH --chown=0:0 "$JB/Applications/Sileo.app/" "$R/Applications/Sileo.app/"
sudo chmod 4755 "$R/Applications/Sileo.app/giveMeRoot"

[ -f "$HERE/jb/ssh_host_ed25519_key" ] || ssh-keygen -q -t ed25519 -N '' -C ios-sunfish -f "$HERE/jb/ssh_host_ed25519_key"
sudo install -d -o 0 -g 0 -m 0700 "$R/private/var/root/.ssh"
sudo install -o 0 -g 0 -m 0600 "$HERE/jb/ssh_host_ed25519_key" "$R/private/var/root/.ssh/ssh_host_ed25519_key"
sudo install -o 0 -g 0 -m 0600 "$KEY" "$R/private/var/root/.ssh/authorized_keys"

sudo python3 -I - "$R/System/Library/xpc/launchd.plist" <<'EOF'
import os, plistlib, sys
path = sys.argv[1]
st = os.stat(path)
plist = plistlib.load(open(path, 'rb'))
jobs = plist['LaunchDaemons']
for k in [k for k, j in jobs.items() if j.get('Label') in ('com.sweet.gputest', 'com.sweet.jbprep', 'com.sweet.sshd')]:
    del jobs[k]
jobs['/System/Library/LaunchDaemons/com.sweet.jbprep.plist'] = {
    'Label': 'com.sweet.jbprep',
    'ProgramArguments': ['/var/jb/bin/sh', '/var/jb/prep.sh'],
    'RunAtLoad': True, 'UserName': 'root',
}
jobs['/System/Library/LaunchDaemons/com.sweet.sshd.plist'] = {
    'Label': 'com.sweet.sshd',
    'ProgramArguments': ['/var/jb/usr/sbin/sshd', '-D', '-e',
                         '-h', '/var/root/.ssh/ssh_host_ed25519_key',
                         '-o', 'UsePAM=no', '-o', 'PasswordAuthentication=no',
                         '-o', 'KbdInteractiveAuthentication=no',
                         '-o', 'PermitRootLogin=prohibit-password',
                         '-o', 'AuthorizedKeysFile=/var/root/.ssh/authorized_keys'],
    'RunAtLoad': True, 'KeepAlive': True, 'UserName': 'root',
}
plistlib.dump(plist, open(path, 'wb'), fmt=plistlib.FMT_BINARY)
os.chown(path, st.st_uid, st.st_gid)
os.chmod(path, st.st_mode & 0o7777)
print(f'launchd.plist: {len(jobs)} jobs, com.sweet.jbprep and com.sweet.sshd in')
EOF

sudo find "$R/private/var/mobile/Containers/Data/InternalDaemon" -name 'com.apple.LaunchServices-*.csstore' -print -delete 2>/dev/null || true
echo "jb-to-root.sh: done ($(sudo du -sh "$R/private/var/jb" | cut -f1) in /private/var/jb)"
