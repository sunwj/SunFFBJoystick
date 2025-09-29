import time
import serial
from serial.tools import list_ports


com = serial.Serial('COM16', baudrate=115200, bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE)


def list_available_ports():
    port_names = []
    ports = list_ports.comports()
    for port, desc, hwid in ports:
        print(f"Port: {port}, Description: {desc}, HWID: {hwid}")
        port_names.append(port)
    return port_names


def send_packet(x, y):
    data = bytes([0x7E, 0x81] + list(int.to_bytes(x, 2, 'little', signed=True)) + list(int.to_bytes(y, 2, 'little', signed=True)))
    data = list(data)
    data.append(0 ^ data[2] ^ data[3] ^ data[4] ^ data[5])
    data = bytes(data)
    
    com.write(data)
    # print(data)


if __name__ == '__main__':
    # list_available_ports()
    x, y = 0, 0
    while True:
        send_packet(x, y)
        x += 1
        y -= 1

        time.sleep(0.002)