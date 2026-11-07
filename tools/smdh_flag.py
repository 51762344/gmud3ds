#!/usr/bin/env python3
"""Set or clear the SMDH "extended banner" flag (HOME menu CGFX banner).

smdhtool has no flag switch, so the 0x2024 ApplicationSettings field is
patched after the SMDH is generated.  bit 5 = extended banner (3dbrew);
the flag must be 0 for the normal 256x128 PNG banner.

    python tools/smdh_flag.py gmud3ds.smdh          # set
    python tools/smdh_flag.py --clear gmud3ds.smdh  # clear
    python tools/smdh_flag.py --check gmud3ds.smdh  # read only
"""

from __future__ import annotations

import struct
import sys

FLAG_EXTENDED_BANNER = 0x20
FLAGS_OFF = 0x2024


def main(argv: list[str]) -> int:
    if '-h' in argv or '--help' in argv:
        print(__doc__)
        return 0
    check = '--check' in argv
    clear = '--clear' in argv
    args = [a for a in argv if a not in ('--check', '--clear')]
    if check and clear:
        print('--check and --clear are mutually exclusive', file=sys.stderr)
        return 2
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    path = args[0]
    with open(path, 'rb') as f:
        data = bytearray(f.read())
    if data[:4] != b'SMDH':
        print('%s: not an SMDH' % path, file=sys.stderr)
        return 1
    if len(data) < FLAGS_OFF + 4:
        print('%s: truncated SMDH' % path, file=sys.stderr)
        return 1

    flags = struct.unpack_from('<I', data, FLAGS_OFF)[0]
    if check:
        print('%s: flags=0x%08x extended_banner=%s' %
              (path, flags, bool(flags & FLAG_EXTENDED_BANNER)))
        return 0 if flags & FLAG_EXTENDED_BANNER else 1

    if clear:
        new = flags & ~FLAG_EXTENDED_BANNER
    else:
        new = flags | FLAG_EXTENDED_BANNER
    if new != flags:
        struct.pack_into('<I', data, FLAGS_OFF, new)
        with open(path, 'wb') as f:
            f.write(data)
    print('%s: flags 0x%08x -> 0x%08x' % (path, flags, new))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
