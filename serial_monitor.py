"""
serial_monitor.py — Simple JSON-line viewer for Ardu1 serial output

Usage:
  python serial_monitor.py --port COM5 --baud 115200
  python serial_monitor.py --port /dev/ttyACM0
"""
import argparse, sys, json, time
import serial

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="Serial port, e.g. COM5 or /dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
    except Exception as e:
        print(f"ERROR: could not open {args.port}: {e}")
        sys.exit(1)

    print(f"Connected to {args.port} @ {args.baud}. Waiting for lines...\n")
    buf = b""
    while True:
        try:
            b = ser.read(1024)
            if b:
                buf += b
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    s = line.decode(errors="ignore").strip()
                    if not s:
                        continue
                    try:
                        obj = json.loads(s)
                        # Pretty minimal view
                        if obj.get("event") == "menu":
                            print(f"[MENU] selection={obj.get('selection')}")
                        elif obj.get("event") == "state":
                            print(f"[STATE] {obj.get('state')}")
                        elif obj.get("event") == "keyword":
                            print(f"[KW] label={obj.get('label')} score={obj.get('score')}")
                        elif obj.get("event") == "select":
                            print(f"[SELECT] option={obj.get('option')}")
                        elif obj.get("event") == "data":
                            print(f"[DATA] option={obj.get('option')} payload={obj.get('payload')}")
                        elif obj.get("event") == "error":
                            print(f"[ERROR@{obj.get('where')}] {obj.get('msg')}")
                        else:
                            print(s)
                    except json.JSONDecodeError:
                        # Non-JSON line from the board; just print it
                        print(s)
            else:
                time.sleep(0.05)
        except KeyboardInterrupt:
            print("\nBye.")
            break

if __name__ == "__main__":
    main()
