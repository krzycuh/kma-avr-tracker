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
    received_data = ser.read()  # read serial port
    sleep(0.3)
    data_left = ser.inWaiting()  # check for remaining byte
    received_data += ser.read(data_left)
    decoded = received_data.decode("utf-8")
    return decoded

# Pause the program for 1 second to avoid overworking the serial port
while 1:
    decoded = receiveData()
    print("decoded: ", decoded)  # print received data
    # ser.write(decoded)

