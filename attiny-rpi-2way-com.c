#include<avr/io.h>
#include<util/delay.h>
#include <stdbool.h>
#include <avr/interrupt.h>

const int delayTime = 600;

#define BAUD 9600
// there is FCPU = 8MHz with division by 8 clock = 1MHz, and we are using U2X bit for better accuracy
// UBRR formula for U2X = 1
#define MYUBBR ((F_CPU / (BAUD * 8L)) - 1)

#define BUFFER_SIZE 16

volatile uint8_t rxBuffer[BUFFER_SIZE];
volatile uint8_t rxBufferHead = 0;
volatile uint8_t rxBufferTail = 0;

void setupDiode() {
    DDRD |= (1 << 2) | (1 << 3);
    PORTD &= ~(1 << 3);
    PORTD &= ~(1 << 2);
}

void toggleDiode(bool value, int position) {
    if (value == false) {
        PORTD &= ~(1 << position);
    } else {
        PORTD |= (1 << position);
    }
}

void infoDiodeOn() {
    toggleDiode(true, 2);
}

void infoDiodeOff() {
    toggleDiode(false, 2);
}

void writeDiodeOn() {
    toggleDiode(true, 3);
}

void writeDiodeOff() {
    toggleDiode(false, 3);
}

// *********************************************************************************************************
// init_uart
// *********************************************************************************************************
void init_uart(void) {
  // set U2X bit so we can get 9600baud with lowest error rate 0.2%
  UCSRA = (1 << U2X);
  // set baud rate
  UBRRH = (uint8_t)(MYUBBR >> 8); 
  UBRRL = (uint8_t)(MYUBBR);
  // enable receive and transmit and NO INTERRUPT
  UCSRB = (1 << RXEN) | (1 << TXEN) ;
  // enable Receive Complete Interrupt:
  UCSRB |= (1 << RXCIE);
  // set frame format
  UCSRC = (0 << USBS) | (3 << UCSZ0); // asynchron 8n1
  // UCSRC = (1 << USBS) | (3 << UCSZ0);
}

// *********************************************************************************************************
// send_uart
// Sends a single char to UART without ISR
// *********************************************************************************************************
void sendCharUart(uint8_t charToSend) {
  // wait for empty data register
  while (!(UCSRA & (1<<UDRE)));
  // set data into data register
  UDR = charToSend;
}

// *********************************************************************************************************
// uart_puts
// Sends a string.
// *********************************************************************************************************
void sendStringUart(const char *stringToSend) {
  while (*stringToSend) {
    sendCharUart(*stringToSend);
    stringToSend++;
  }
}

// *********************************************************************************************************
// uart_puts
// Sends a string.
// *********************************************************************************************************
void sendBytesUart(uint8_t *bytesToSend) {
  while (*bytesToSend) {
    sendCharUart(*bytesToSend);
    bytesToSend++;
  }
}

void startupInfo() {
    for (int i = 0; i < 3; i++) {
      writeDiodeOn();
      infoDiodeOn();
      _delay_ms(delayTime);
      infoDiodeOff();
      writeDiodeOff();
      _delay_ms(delayTime);
    }

}

// *********************************************************************************************************
// uart interrupts
// receive a byte.
// *********************************************************************************************************
ISR(USART_RX_vect) {
    // RX ISR - Handle received data
    uint8_t receivedData = UDR;

    writeDiodeOn();
    // Check if there's space in the buffer
    if (((rxBufferHead + 1) % BUFFER_SIZE) != rxBufferTail) {
        // Store the received data in the buffer
        rxBuffer[rxBufferHead] = receivedData;
        rxBufferHead = (rxBufferHead + 1) % BUFFER_SIZE;
    }
    // _delay_ms(delayTime);
    writeDiodeOff();

    // Your code to process the received data can be placed here
}

// Function to check if there is data in the buffer
uint8_t isDataAvailable() {
    return rxBufferHead != rxBufferTail;
}

// Function to read a byte from the buffer
uint8_t readByteFromBuffer() {
    uint8_t data = rxBuffer[rxBufferTail];
    rxBufferTail = (rxBufferTail + 1) % BUFFER_SIZE;
    return data;
}

void readFromBufferAndSend() {
  uint8_t receivedBytes[70];
  int receivedBytesLength = 0;
  while (rxBufferHead != rxBufferTail) {
      // Process the received byte
      uint8_t receivedByte = readByteFromBuffer();
      // sendCharUart(receivedByte);
      // _delay_ms(700);
      receivedBytes[receivedBytesLength] = receivedByte;
      receivedBytesLength++;
  }

  if (receivedBytesLength > 0) {
      // sendStringUart("<avr>");
      sendBytesUart(receivedBytes);
      // sendStringUart("</avr>");
  }

  for (int i = 0; i < 70; i++) {
      receivedBytes[i] = 0;
  }

}

int main(void) {
    sei();
    init_uart();
    startupInfo();
    
    while (1) {
        if (isDataAvailable()) {
            writeDiodeOn();

            readFromBufferAndSend();

            _delay_ms(delayTime);
            writeDiodeOff();
        }


        infoDiodeOn();
        _delay_ms(delayTime);
        infoDiodeOff();
        _delay_ms(delayTime);
    }
}
