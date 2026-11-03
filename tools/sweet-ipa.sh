#!/bin/bash
# Install an .ipa on the phone over SSH, without installd or an Apple signature:
#   tools/sweet-ipa.sh APP.ipa [HOST]        (default 192.168.7.2)
#   tools/sweet-ipa.sh --remove BUNDLE.app [HOST]
# The .app goes to /var/jb/Applications beside Sileo and is registered with uicache. Binaries without a signature get an
# ad-hoc one (ldid -S, keeping their entitlements). App Store IPAs whose main binary is FairPlay-encrypted (cryptid 1)
# are refused: they cannot run here, fairplayd has no driver to decrypt them.
set -euo pipefail
LDID=${LDID:-$HOME/theos/toolchain/linux/iphone/bin/ldid}
if [ "${1:-}" = --remove ]; then
	host=${3:-192.168.7.2}
	ssh root@"$host" "uicache -u '/var/jb/Applications/$2'; rm -rf '/var/jb/Applications/$2'"
	exit 0
fi
ipa=${1:?usage: sweet-ipa.sh APP.ipa [HOST]}
host=${2:-192.168.7.2}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
unzip -q "$ipa" -d "$tmp"
app=$(find "$tmp/Payload" -maxdepth 1 -name '*.app' | head -1)
[ -n "$app" ] || { echo "no Payload/*.app in $ipa" >&2; exit 1; }
name=$(basename "$app")
exe=$(python3 -I -c 'import plistlib,sys; print(plistlib.load(open(sys.argv[1],"rb"))["CFBundleExecutable"])' "$app/Info.plist")
if llvm-otool -l "$app/$exe" 2>/dev/null | grep -A4 LC_ENCRYPTION_INFO | grep -q "cryptid 1"; then
	echo "$name: FairPlay-encrypted (an App Store copy); it cannot run here. Use a decrypted or self-built IPA." >&2
	exit 2
fi
# every Mach-O without a signature gets an ad-hoc one
while IFS= read -r f; do
	if file -b "$f" | grep -q Mach-O && ! llvm-otool -l "$f" 2>/dev/null | grep -q LC_CODE_SIGNATURE; then
		echo "signing ${f#"$tmp"/}"
		"$LDID" -S "$f" 2>/dev/null
	fi
done < <(find "$app" -type f)
tar -C "$tmp/Payload" -cf - "$name" | ssh root@"$host" "cd /var/jb/Applications && rm -rf '$name' && tar -xf - && chown -R root:wheel '$name' && uicache -p '/var/jb/Applications/$name'"
echo "installed $name"
