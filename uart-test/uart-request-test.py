import serial
from time import sleep

ser = serial.Serial(
    # Serial Port to read the data from
    port='/dev/ttyS0',

    # Rate at which the information is shared to the communication channel
    baudrate=9600,

    # Applying Parity Checking (none in this case)
    # parity=serial.PARITY_NONE,
    #
    # # Pattern of Bits to be read
    # stopbits=serial.STOPBITS_ONE,
    #
    # # Total number of bits to be read
    # bytesize=serial.EIGHTBITS,

    # Number of serial commands to accept before timing out
    # timeout=1
)

def receiveData():
    sleep(1.3)
    received_data = ser.read()  # read serial port
    # print(received_data)
    sleep(1.3)
    data_left = ser.inWaiting()  # check for remaining byte
    print("[data_left]: ", data_left)
    received_data += ser.read(data_left)

    ser.flushInput()
    print("a: ")
    print(received_data)
    decoded = received_data.decode("utf-8")
    return decoded

def main():
    while 1:
        mode = input("Data to send (v5): ")
        ser.reset_output_buffer()
        ser.write(mode.encode())

        # Pause the program for 1 second to avoid overworking the serial port
        received = receiveData()
        print("Selected num (received from avr): " + received)  # print received data

main()