#!/usr/bin/env python
# Standalone ELRS "Unified" firmware config inspector / transplanter.
# No ELRS imports (avoids the circular-import problem). Mirrors
# UnifiedConfiguration.findFirmwareEnd + appendToFirmware layout exactly.
#
#   inspect: python elrs_cfg_tool.py inspect <firmware.bin>
#   graft  : python elrs_cfg_tool.py graft  <src_with_config.bin> <dst_raw.bin> <out.bin>
#            (copies product+lua+options+layout+tail from SRC onto DST's image)
import sys, struct, json

PRODUCT_LEN, DEVICE_LEN, DEFINES_LEN, LAYOUT_LEN = 128, 16, 512, 2048

def find_firmware_end(f):
    f.seek(0, 0)
    (magic, segments, _, _, _) = struct.unpack('<BBBBI', f.read(8))
    if magic != 0xe9:
        sys.exit('Not an ESP firmware image (bad magic 0x%02x)' % magic)
    is8285 = False
    if segments == 2:            # ESP8266/85 assumption
        f.seek(0x1000, 0)
        (magic, segments, _, _, _) = struct.unpack('<BBBBI', f.read(8))
        is8285 = True
    else:
        f.seek(24, 0)
    for _ in range(segments):
        (_, size) = struct.unpack('<II', f.read(8))
        f.seek(size, 1)
    pos = f.tell()
    pos = (pos + 16) & ~15
    if not is8285:
        pos = pos + 32
    return pos

def read_config(path):
    with open(path, 'rb') as f:
        end = find_firmware_end(f)
        f.seek(end, 0)
        product = f.read(PRODUCT_LEN).split(b'\0', 1)[0].decode(errors='replace')
        lua     = f.read(DEVICE_LEN).split(b'\0', 1)[0].decode(errors='replace')
        defines = f.read(DEFINES_LEN).split(b'\0', 1)[0].decode(errors='replace')
        layout  = f.read(LAYOUT_LEN).split(b'\0', 1)[0].decode(errors='replace')
        tail    = f.read()  # logo + prior_target_name marker, if any
        f.seek(0, 2); total = f.tell()
    return dict(end=end, total=total, product=product, lua=lua,
                defines=defines, layout=layout, tail_len=len(tail))

def inspect(path):
    c = read_config(path)
    print("file           : %s" % path)
    print("image ends at  : 0x%x (%d)   file size: %d" % (c['end'], c['end'], c['total']))
    print("product_name   : %r" % c['product'])
    print("lua_name       : %r" % c['lua'])
    print("--- options JSON (bind/wifi/domain) ---")
    if c['defines']:
        try:
            d = json.loads(c['defines'])
            for k in sorted(d):
                print("   %-24s = %s" % (k, d[k]))
        except Exception as e:
            print("   (raw, unparsed) %r  [%s]" % (c['defines'], e))
    else:
        print("   <empty -> BARE image, no options baked>")
    print("--- hardware layout JSON (key pins) ---")
    if c['layout']:
        try:
            h = json.loads(c['layout'])
            keys = ['product_name','serial_rx','serial_tx','led_rgb','led','led_red',
                    'radio_nss','radio_nss_2','radio_busy','radio_dio1','radio_dio1_2',
                    'radio_rst','radio_miso','radio_mosi','radio_sck','radio_rfsw_ctrl',
                    'power_min','power_max','power_default','power_lna_gain']
            print("   %d layout keys total; selected:" % len(h))
            for k in keys:
                if k in h:
                    print("     %-20s = %s" % (k, h[k]))
        except Exception as e:
            print("   (raw, unparsed) len=%d  [%s]" % (len(c['layout']), e))
    else:
        print("   <empty -> no layout baked (bare)>")
    print("tail bytes     : %d (logo/prior-target marker)" % c['tail_len'])

def graft(src, dst, out):
    # pull the entire config region (everything past SRC's image end) verbatim
    with open(src, 'rb') as f:
        end = find_firmware_end(f)
        f.seek(end, 0)
        blob = f.read()
    # truncate DST to its own image end, then append SRC's config blob
    with open(dst, 'rb') as f:
        dst_end = find_firmware_end(f)
        f.seek(0, 0); dst_image = f.read(dst_end)
    with open(out, 'wb') as f:
        f.write(dst_image)
        f.write(blob)
    print("grafted %d config bytes from %s onto %s (image end 0x%x) -> %s"
          % (len(blob), src, dst, dst_end, out))

if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == 'inspect':
        inspect(sys.argv[2])
    elif len(sys.argv) == 5 and sys.argv[1] == 'graft':
        graft(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        print(__doc__); sys.exit(1)
