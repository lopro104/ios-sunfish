#!/usr/bin/env python3
"""Add /product/camera with "flash" = 1 to an ADT (the rear torch, xnu 0151 kern.sweet_torch).

usage: sweet-adt-flash.py IN.adt OUT.adt

MobileGestalt's camera-flash is /product/camera "flash" (the iPhone X's own device tree,
research/ipsw-x/d22ap.adt, has it as 0x1); without it Control Center shows no flashlight button.
Only that property is added: the rest of the iPhone's camera node describes hardware we do not
have. IN is the full ADT (shim/out/sunfish.adt); it is not regenerated, only extended.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt  # noqa: E402


def phandles(node, out):
    v = node.prop('AAPL,phandle')
    if v is not None and len(v) == 4:
        out.append(struct.unpack('<I', v)[0])
    for c in node.children:
        phandles(c, out)
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    buf = adt.load(sys.argv[1])
    root, end = adt.parse(buf)
    assert adt.serialize(root) == buf[:end], 'ADT does not round-trip'
    product = next((c for c in root.children if c.name == 'product'), None)
    assert product is not None, 'no /product node'
    camera = next((c for c in product.children if c.name == 'camera'), None)
    if camera is None:
        camera = adt.Node()
        camera.props.append(('name', 0, b'camera\0'))
        camera.props.append(('AAPL,phandle', 0, struct.pack('<I', max(phandles(root, [])) + 1)))
        product.children.append(camera)
    camera.props = [p for p in camera.props if p[0] != 'flash']
    camera.props.append(('flash', 0, struct.pack('<I', 1)))
    with open(sys.argv[2], 'wb') as f:
        f.write(adt.serialize(root))


if __name__ == '__main__':
    main()
