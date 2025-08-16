#include<avr/io.h>
#include<util/delay.h>
#include <stdbool.h>

const int delayTime = 600;

#define BAUD 9600
// there is FCPU = 8MHz with division by 8 clock = 1MHz, and we are using U2X bit for better accuracy
// UBRR formula for U2X = 1
#define MYUBBR ((F_CPU / (BAUD * 8L)) - 1)

void setupDiode() {
    DDRD |= 1 << 2;
    PORTD &= ~(1 << 2);
}

void toggleDiode(bool value) {
    if (value == false) {
        PORTD &= ~(1 << 2);
    } else {
        PORTD |= (1 << 2);
    }
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

void doMain(int counter) {
    toggleDiode(true);
    char messageToSend[50] = "co slychac? łąźćńóż";
    sendStringUart(messageToSend);

    _delay_ms(delayTime);
    toggleDiode(false);
}


int main(void) {
    init_uart();
    toggleDiode(false);
    int counter = 0;
    while (1) {
        counter =+ 1;
        _delay_ms(delayTime);
        doMain(counter);
    }
}
