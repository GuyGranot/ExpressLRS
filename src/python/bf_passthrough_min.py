#!/usr/bin/env python
# Minimal, self-contained Betaflight passthrough enabler.
# Does ONLY what BFinitPassthrough.bf_passthrough_init() does -- enter the FC CLI,
# find the serial-RX UART, and issue `serialpassthrough <uart> <baud>` -- WITHOUT
# pulling in ELRS's circular external/ import chain. It does NOT try to reboot the
# ELRS app into its bootloader (a dead app can't honor that); you hold BOOT for that.
#
# Usage:  python bf_passthrough_min.py COM6 420000
import sys, time, re, serial

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM6"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 420000

    print("== opening %s @ 115200 for CLI ==" % port)
    s = serial.Serial(port=port, baudrate=115200, bytesize=8, parity='N',
                      stopbits=1, timeout=1.0, xonxoff=0, rtscts=0)

    def send(line):
        s.write((line + "\r\n").encode()); s.flush()

    # Enter CLI
    s.write(b"#"); s.flush()
    time.sleep(0.4)
    start = s.read(4096).decode(errors="replace")
    if "CCC" in start:
        print("!! FC is already streaming 'CCC' -- looks already in passthrough. "
              "Skip this step and go straight to esptool.")
        s.close(); return 0
    if "#" not in start:
        print("!! No CLI prompt returned. The FC may already be in passthrough, or "
              "not on this port. If esptool then fails, reboot the FC and retry.")

    # Dump the serial map and find the RX UART (function mask bit 64 = SERIAL_RX)
    send("serial")
    time.sleep(0.5)
    dump = s.read(8192).decode(errors="replace")
    rx_idx = None
    for line in dump.splitlines():
        line = line.strip()
        m = re.search(r'serial ((?:UART)?[0-9]+) ([0-9]+) ', line)
        if m and (int(m.group(2)) & 64) == 64:
            rx_idx = m.group(1)
            print("   RX serial UART detected: %s" % line)
            break

    if rx_idx is None:
        print("!! Could not auto-detect the serial-RX UART from the `serial` output.")
        print("   Full dump follows -- find the line whose 2nd number has bit 64 set:")
        print(dump)
        print("   Then run in BF CLI manually:  serialpassthrough <thatUART> %d" % baud)
        s.close(); return 1

    cmd = "serialpassthrough %s %d" % (rx_idx, baud)
    print("== sending: %s ==" % cmd)
    send(cmd)
    time.sleep(0.3)
    s.close()
    print("== PASSTHROUGH ENABLED. %s freed; FC stays bridged until it reboots. ==" % port)
    print("   Now run the esptool probe.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
