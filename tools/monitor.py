#!/usr/bin/env python3
"""
Serial monitor for the board.

`idf.py monitor` is not usable here: the build runs in a container and Docker
Desktop on macOS cannot pass through a USB serial port. This does the small
part of it that matters -- pulse the board into reset so the boot log is not
missed, then stream until Ctrl-C.

    python3 tools/monitor.py [port] [--no-reset]
"""
import sys
import time
import glob

try:
    import serial
except ImportError:
    sys.exit("pyserial missing. Try: ~/.esptool-venv/bin/python tools/monitor.py")

BAUD = 115200


def find_port():
    """The CH343 bridge on this board enumerates as usbmodem, not usbserial."""
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*")
                   + glob.glob("/dev/cu.wchusbserial*"))
    if not ports:
        sys.exit("No serial port found. Is the cable in the UART socket?")
    if len(ports) > 1:
        print(f"note: several ports found, using {ports[0]} ({', '.join(ports[1:])} ignored)",
              file=sys.stderr)
    return ports[0]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    reset = "--no-reset" not in sys.argv

    port = args[0] if args else find_port()
    ser = serial.Serial(port, BAUD, timeout=0.2)

    # DTR/RTS drive EN and BOOT through the auto-reset circuit. Leave DTR low so
    # the chip stays out of download mode, and pulse RTS to reset it.
    ser.setDTR(False)
    if reset:
        ser.setRTS(True)
        time.sleep(0.1)
        ser.setRTS(False)
    else:
        ser.setRTS(False)

    print(f"--- {port} @ {BAUD}, Ctrl-C to quit ---", file=sys.stderr)
    try:
        while True:
            data = ser.read(4096)
            if data:
                sys.stdout.write(data.decode("utf-8", "replace"))
                sys.stdout.flush()
    except KeyboardInterrupt:
        print("\n--- closed ---", file=sys.stderr)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
