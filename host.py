import hid
import time

print('test')

VID = 0x303a  # Replace with your ESP32-S3's Vendor ID
PID = 0x1001  # Replace with your ESP32-S3's Product ID

def main():
    device = hid.Device(VID, PID)
    print(f"Connected to device: {device.product} ({device.manufacturer})")

    while True:
        # Read HID report (3 bytes: 2 for angle, 1 for buttons)
        report = device.read(3)
        if len(report) == 3:
            x = int.from_bytes(report[1:], byteorder='little', signed=False)
            print(f"X: {x:.2f}")

            # Send force feedback (example: proportional to angle)
            y = (x * 2).to_bytes(1, byteorder='little', signed=False)
            print(int.from_bytes(y, byteorder='little', signed=False))
            device.write(bytes([0] + list(y)))

        time.sleep(0.1)

if __name__ == "__main__":
    main()
