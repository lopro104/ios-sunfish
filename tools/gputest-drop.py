#!/usr/bin/env python3
"""Make what the kernel's file drop (patch 0103) puts onto the root for the Mesa GPU test.

  gputest-drop.py LAUNCHD_PLIST MESA_OUT OUT_DIR

LAUNCHD_PLIST is the IPSW's own /System/Library/xpc/launchd.plist (from the OS image, e.g. through
apfs-fuse); MESA_OUT the CI artifact mesa-ios's out/ (libOSMesa.8.dylib, gputest). OUT_DIR gets
launchd.plist and drop.txt, the spec for `mkadt.py --drop OUT_DIR/drop.txt`.

The launchd.plist written over the root's must be what the root has (tools/sweet-root-tweaks.py's
launchd(): _PanicOnCrash renamed, CommCenter removed), plus one job: com.sweet.gputest, which runs
/usr/bin/gputest once at boot.
"""
import os, plistlib, sys

src, mesa, out = sys.argv[1:4]
os.makedirs(out, exist_ok=True)
with open(src, 'rb') as f:
    plist = plistlib.load(f)
jobs = plist['LaunchDaemons']
# tools/sweet-root-tweaks.py launchd(), the same changes
for job in jobs.values():
    if '_PanicOnCrash' in job:
        job['_PanicOnCrasX'] = job.pop('_PanicOnCrash')
for k in [k for k, j in jobs.items() if j.get('Label') == 'com.apple.CommCenter']:
    del jobs[k]
jobs['/System/Library/LaunchDaemons/com.sweet.gputest.plist'] = {
    'Label': 'com.sweet.gputest',
    'ProgramArguments': ['/usr/bin/gputest'],
    'RunAtLoad': True,
    'POSIXSpawnType': 'Interactive',
    'UserName': 'root',
    # Without a limit jetsam killed it (reason code 7, per-process limit) while Mesa (16 MB) made its context.
    'JetsamProperties': {'JetsamMemoryLimit': 512},
}
with open(os.path.join(out, 'launchd.plist'), 'wb') as f:
    plistlib.dump(plist, f, fmt=plistlib.FMT_BINARY)
with open(os.path.join(out, 'drop.txt'), 'w') as f:
    f.write(f'{os.path.abspath(out)}/launchd.plist /System/Library/xpc/launchd.plist 0644\n')
    f.write(f'{os.path.abspath(mesa)}/libOSMesa.8.dylib /usr/lib/libOSMesa.8.dylib 0755\n')
    f.write(f'{os.path.abspath(mesa)}/gputest /usr/bin/gputest 0755\n')
print(f'{out}/launchd.plist: {len(jobs)} jobs; {out}/drop.txt', file=sys.stderr)
