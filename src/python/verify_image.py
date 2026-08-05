# Inspect (and diff) the configuration blob that binary_configurator.py appends to
# a firmware image, so a bad image is caught before it costs a flash cycle.
#
# binary_configurator writes ONE options blob, and options_LoadFromFlashOrFile()
# prefers that blob over anything in LittleFS whenever the flash-discriminator has
# changed -- which it does on every build, since it is random. So an option that is
# absent here is not "left as it was on the device", it is reset to the firmware
# default at the next boot. The defaults are not all benign: a missing "domain" is
# AU915 regardless of the compile-time Regulatory_Domain, which on an LR1121
# receiver means it comes up on the wrong sub-GHz band and never links.
#
#     python verify_image.py firmware-new.bin
#     python verify_image.py firmware-new.bin --against firmware-known-good.bin
#     python verify_image.py firmware-new.bin --expect-domain 1 --expect-uid
#
# Exit status is non-zero if the image is bare, malformed, or fails an --expect.
# --against reports differences but does not by itself fail: a new build is always
# expected to differ in flash-discriminator.

import argparse
import json
import struct
import sys

# Blob layout, from UnifiedConfiguration.appendToFirmware()
PRODUCT_BYTES = 128
LUA_BYTES = 16
DEFINES_BYTES = 512
LAYOUT_BYTES = 2048

# Options that differ between every build by design, so --against ignores them.
VOLATILE_OPTIONS = ("flash-discriminator",)

# Index into FHSS.cpp's domains[]; the order is fixed by
# binary_configurator.domain_number(). Only meaningful on SX127x/LR1121 targets,
# where domains[] has eight entries; SX128x has exactly one.
DOMAIN_NAMES = ["au_915", "fcc_915", "eu_868", "in_866",
                "au_433", "eu_433", "us_433", "us_433_wide"]


def find_firmware_end(f):
    f.seek(0, 0)
    (magic, segments, _, _, _) = struct.unpack('<BBBBI', f.read(8))
    if magic != 0xe9:
        raise ValueError('not an ESP firmware image (bad magic)')
    is8285 = False
    if segments == 2:  # assume ESP8266/85
        f.seek(0x1000, 0)
        (magic, segments, _, _, _) = struct.unpack('<BBBBI', f.read(8))
        is8285 = True
    else:
        f.seek(24, 0)
    for _ in range(segments):
        (_, size) = struct.unpack('<II', f.read(8))
        f.seek(size, 1)
    pos = (f.tell() + 16) & ~15
    if not is8285:
        pos += 32
    return pos


def _cstr(b):
    return b.split(b'\0', 1)[0].decode(errors='replace')


def read_image(path):
    with open(path, 'rb') as f:
        end = find_firmware_end(f)
        f.seek(end)
        product = _cstr(f.read(PRODUCT_BYTES))
        lua = _cstr(f.read(LUA_BYTES))
        defines = _cstr(f.read(DEFINES_BYTES))
        layout = _cstr(f.read(LAYOUT_BYTES))
        trailing = len(f.read())

    img = {'path': path, 'offset': end, 'product_name': product, 'lua_name': lua,
           'trailing': trailing, 'errors': []}
    try:
        img['options'] = json.loads(defines) if defines else {}
    except ValueError as e:
        img['options'] = None
        img['errors'].append('options JSON is unparseable: %s' % e)
    try:
        img['layout'] = json.loads(layout) if layout else {}
    except ValueError as e:
        img['layout'] = None
        img['errors'].append('hardware layout JSON is unparseable: %s' % e)
    return img


def describe(img):
    print('%s' % img['path'])
    print('  blob offset  : 0x%X (%d trailing bytes: logo / prior target name)'
          % (img['offset'], img['trailing']))
    print('  product_name : %r' % img['product_name'])
    print('  lua_name     : %r' % img['lua_name'])
    if img['options'] is None:
        print('  options      : UNPARSEABLE')
    else:
        for k in sorted(img['options']):
            note = ''
            if k == 'domain':
                v = img['options'][k]
                note = '   <- %s' % (DOMAIN_NAMES[v] if v < len(DOMAIN_NAMES) else '???')
            if k == 'uid':
                note = '   <- binding phrase baked in'
            print('    %-26s %s%s' % (k, json.dumps(img['options'][k]), note))
        if not img['options']:
            print('    (none)')
    if img['layout'] is None:
        print('  layout       : UNPARSEABLE')
    else:
        print('  layout       : %d keys%s' % (len(img['layout']),
              '' if img['layout'] else '   <- BARE, no hardware layout baked'))


# Checks that hold for any image meant to be flashed to a specific product.
def check(img, args):
    problems = list(img['errors'])
    if img['product_name'] in ('', 'Unified'):
        problems.append('image is BARE (product_name %r): no hardware layout was baked, '
                        'so the receiver comes up with no LED, no RF switch and a dead '
                        'radio' % img['product_name'])
    if img['layout'] is not None and not img['layout']:
        problems.append('hardware layout is empty')
    opts = img['options'] or {}

    if args.expect_uid and 'uid' not in opts:
        problems.append('no "uid": the image relies on whatever bind is already in the '
                        'receiver\'s NVS rather than carrying its own')
    if args.expect_domain is not None:
        got = opts.get('domain')
        if got != args.expect_domain:
            problems.append('"domain" is %s, expected %s (%s). A missing key reads as 0 = '
                            'au_915 at runtime, whatever Regulatory_Domain was compiled in'
                            % (got, args.expect_domain,
                               DOMAIN_NAMES[args.expect_domain]
                               if args.expect_domain < len(DOMAIN_NAMES) else '???'))
    if args.expect_auto_wifi and 'wifi-on-interval' not in opts:
        problems.append('no "wifi-on-interval": WiFi auto-start is disabled, which removes '
                        'the only way back into a receiver that will not link')
    return problems


def diff_options(new, old):
    a = new['options'] or {}
    b = old['options'] or {}
    rows = []
    for k in sorted(set(a) | set(b)):
        if k in VOLATILE_OPTIONS:
            continue
        if a.get(k) != b.get(k):
            rows.append((k, b.get(k, '<absent>'), a.get(k, '<absent>')))
    return rows


def main(custom_args=None):
    p = argparse.ArgumentParser(
        description='Inspect and diff the configuration blob in an ELRS firmware image')
    p.add_argument('image', help='firmware .bin to inspect')
    p.add_argument('--against', metavar='KNOWN_GOOD_BIN',
                   help='report how this image\'s baked options differ from a proven one')
    p.add_argument('--expect-uid', action='store_true',
                   help='fail unless a binding UID is baked in')
    p.add_argument('--expect-domain', type=int, metavar='N',
                   help='fail unless "domain" is N (1 = fcc_915). Only meaningful on '
                        'sub-GHz-capable targets')
    p.add_argument('--expect-auto-wifi', action='store_true',
                   help='fail unless WiFi auto-start is configured')
    args = p.parse_args(custom_args)

    try:
        img = read_image(args.image)
    except (OSError, ValueError) as e:
        print('ERROR: %s' % e)
        return 2
    describe(img)

    if args.against:
        try:
            old = read_image(args.against)
        except (OSError, ValueError) as e:
            print('ERROR reading --against image: %s' % e)
            return 2
        print('')
        print('vs %s' % args.against)
        if img['product_name'] != old['product_name']:
            print('  product_name differs: %r -> %r  <- DIFFERENT PRODUCT, check the target'
                  % (old['product_name'], img['product_name']))
        if (img['layout'] or {}) != (old['layout'] or {}):
            print('  hardware layout differs')
        rows = diff_options(img, old)
        if not rows:
            print('  baked options are identical (ignoring %s)' % ', '.join(VOLATILE_OPTIONS))
        else:
            print('  %-26s %-24s %s' % ('option', 'known good', 'this image'))
            for k, o, n in rows:
                print('  %-26s %-24s %s' % (k, json.dumps(o), json.dumps(n)))

    problems = check(img, args)
    print('')
    if problems:
        for pr in problems:
            print('FAIL: %s' % pr)
        return 1
    print('OK: image carries a product layout and passes the requested checks')
    return 0


if __name__ == '__main__':
    sys.exit(main())
