import serial
import sys
import time

port = '/dev/ttyUSB0'
print(f"Opening {port}...")
ser = serial.Serial(port, 115200, timeout=1.0)

# Send command to start UWB just in case
ser.write(b"AT\r\n")
time.sleep(0.5)
while ser.in_waiting:
    print("RAW:", ser.readline().decode('utf-8', errors='ignore').strip())

print("Listening...")
start_time = time.time()
while time.time() - start_time < 5.0:
    if ser.in_waiting:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        print("RAW:", line)
    else:
        time.sleep(0.01)

ser.close()
