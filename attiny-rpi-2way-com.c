#include<avr/io.h>
#include<util/delay.h>
#include <stdbool.h>
#include <avr/interrupt.h>

// Czas opóźnienia w milisekundach używany do migania diód LED
const int delayTime = 600;

// Parametry komunikacji UART
#define BAUD 9600   // Prędkość transmisji 9600 bps
// Uwaga: FCPU = 8MHz z dzielnikiem /8 = 1MHz, używamy bitu U2X dla lepszej dokładności
// Formuła UBRR dla U2X = 1
#define MYUBBR ((F_CPU / (BAUD * 8L)) - 1)

// Rozmiar bufora kołowego do przechowywania odebranych danych
#define BUFFER_SIZE 16

// Bufor kołowy dla odebranych danych UART - volatile bo używany w przerwaniach
volatile uint8_t rxBuffer[BUFFER_SIZE];
volatile uint8_t rxBufferHead = 0;  // Wskaźnik na następne miejsce do zapisu
volatile uint8_t rxBufferTail = 0;  // Wskaźnik na następne miejsce do odczytu

// Konfiguracja diod LED - ustawia piny PD2 i PD3 jako wyjścia i wyłącza diody
void setupDiode() {
    DDRD |= (1 << 2) | (1 << 3);  // Ustaw PD2 i PD3 jako wyjścia (diody)
    PORTD &= ~(1 << 3);           // Wyłącz diodę "write" (PD3)
    PORTD &= ~(1 << 2);           // Wyłącz diodę "info" (PD2)
}

// Uniwersalna funkcja do włączania/wyłączania diody na określonym pinie
void toggleDiode(bool value, int position) {
    if (value == false) {
        PORTD &= ~(1 << position);  // Wyłącz diodę (stan niski)
    } else {
        PORTD |= (1 << position);   // Włącz diodę (stan wysoki)
    }
}

// Włącza diodę informacyjną (PD2) - sygnalizuje normalną pracę programu
void infoDiodeOn() {
    toggleDiode(true, 2);
}

// Wyłącza diodę informacyjną (PD2)
void infoDiodeOff() {
    toggleDiode(false, 2);
}

// Włącza diodę zapisu (PD3) - sygnalizuje odbieranie/wysyłanie danych UART
void writeDiodeOn() {
    toggleDiode(true, 3);
}

// Wyłącza diodę zapisu (PD3)
void writeDiodeOff() {
    toggleDiode(false, 3);
}

// *********************************************************************************************************
// Inicjalizacja UART - konfiguruje port szeregowy z przerwaniami
// *********************************************************************************************************
void init_uart(void) {
  // Ustawienie bitu U2X dla podwójnej prędkości - błąd tylko 0.2% przy 9600 bps
  UCSRA = (1 << U2X);

  // Konfiguracja prędkości transmisji (baud rate)
  UBRRH = (uint8_t)(MYUBBR >> 8);  // Starszy bajt rejestru UBRR
  UBRRL = (uint8_t)(MYUBBR);       // Młodszy bajt rejestru UBRR

  // Włączenie odbiornika i nadajnika UART
  UCSRB = (1 << RXEN) | (1 << TXEN);

  // Włączenie przerwania od zakończenia odbioru danych
  UCSRB |= (1 << RXCIE);

  // Konfiguracja formatu ramki: 8 bitów danych, 1 bit stopu, brak parzystości (8N1)
  UCSRC = (0 << USBS) | (3 << UCSZ0); // asynchron 8n1
}

// *********************************************************************************************************
// Wysyłanie pojedynczego znaku przez UART (bez używania przerwań)
// *********************************************************************************************************
void sendCharUart(uint8_t charToSend) {
  // Oczekuj aż rejestr danych będzie gotowy do zapisu
  while (!(UCSRA & (1<<UDRE)));
  // Zapisz dane do rejestru transmisji
  UDR = charToSend;
}

// *********************************************************************************************************
// Wysyłanie ciągu znaków (string) przez UART
// *********************************************************************************************************
void sendStringUart(const char *stringToSend) {
  while (*stringToSend) {
    sendCharUart(*stringToSend);
    stringToSend++;
  }
}

// *********************************************************************************************************
// Wysyłanie tablicy bajtów przez UART (zakończone bajtem 0x00)
// *********************************************************************************************************
void sendBytesUart(uint8_t *bytesToSend) {
  while (*bytesToSend) {
    sendCharUart(*bytesToSend);
    bytesToSend++;
  }
}

// Sekwencja startowa - 3-krotne miganie obiema diodami sygnalizujące uruchomienie systemu
void startupInfo() {
    for (int i = 0; i < 3; i++) {
      writeDiodeOn();   // Włącz diodę zapisu
      infoDiodeOn();    // Włącz diodę informacyjną
      _delay_ms(delayTime);  // Pauza z włączonymi diodami
      infoDiodeOff();   // Wyłącz diodę informacyjną
      writeDiodeOff();  // Wyłącz diodę zapisu
      _delay_ms(delayTime);  // Pauza z wyłączonymi diodami
    }
}

// *********************************************************************************************************
// Procedura obsługi przerwania UART - wywoływana przy odbiorze każdego bajtu
// *********************************************************************************************************
ISR(USART_RX_vect) {
    // Odczytaj odebrany bajt z rejestru danych UART
    uint8_t receivedData = UDR;

    writeDiodeOn();  // Sygnalizacja aktywności odbioru diodą LED

    // Sprawdź czy jest miejsce w buforze kołowym (czy bufor nie jest pełny)
    if (((rxBufferHead + 1) % BUFFER_SIZE) != rxBufferTail) {
        // Zapisz odebrany bajt w buforze
        rxBuffer[rxBufferHead] = receivedData;
        // Przesuń wskaźnik głowy bufora (z zawinięciem)
        rxBufferHead = (rxBufferHead + 1) % BUFFER_SIZE;
    }
    // Jeśli bufor jest pełny, dane są odrzucane (brak obsługi overflow)

    writeDiodeOff();  // Wyłącz sygnalizację odbioru
}

// Sprawdza czy w buforze kołowym znajdują się nieodczytane dane
uint8_t isDataAvailable() {
    return rxBufferHead != rxBufferTail;  // Różne wskaźniki = dane dostępne
}

// Odczytuje jeden bajt z bufora kołowego i przesuwa wskaźnik ogona
uint8_t readByteFromBuffer() {
    uint8_t data = rxBuffer[rxBufferTail];  // Pobierz bajt z pozycji ogona
    rxBufferTail = (rxBufferTail + 1) % BUFFER_SIZE;  // Przesuń ogon (z zawinięciem)
    return data;
}

// Główna funkcja przetwarzania - odczytuje wszystkie dane z bufora i retransmituje je
void readFromBufferAndSend() {
  uint8_t receivedBytes[70];  // Tymczasowa tablica na odebrane bajty
  int receivedBytesLength = 0;

  // Odczytaj wszystkie dostępne dane z bufora kołowego
  while (rxBufferHead != rxBufferTail) {
      uint8_t receivedByte = readByteFromBuffer();
      receivedBytes[receivedBytesLength] = receivedByte;
      receivedBytesLength++;
  }

  // Jeśli odebrano jakieś dane, wyślij je z powrotem przez UART
  if (receivedBytesLength > 0) {
      sendBytesUart(receivedBytes);  // Echo - retransmisja odebranych danych
  }

  // Wyczyść tablicę tymczasową (bezpieczeństwo)
  for (int i = 0; i < 70; i++) {
      receivedBytes[i] = 0;
  }
}

// *********************************************************************************************************
// Główna funkcja programu - obsługuje komunikację dwukierunkową UART z sygnalizacją LED
// *********************************************************************************************************
int main(void) {
    // Włącz obsługę przerwań globalnie
    sei();

    // Zainicjalizuj komunikację UART z przerwaniami
    init_uart();

    // Wykonaj sekwencję startową (3x miganie diód)
    startupInfo();

    // Główna pętla programu - działa w nieskończoność
    while (1) {
        // Sprawdź czy w buforze znajdują się odebrane dane
        if (isDataAvailable()) {
            writeDiodeOn();        // Sygnalizuj przetwarzanie danych

            readFromBufferAndSend();  // Odczytaj dane z bufora i prześlij je dalej

            _delay_ms(delayTime);  // Krótka pauza
            writeDiodeOff();       // Zakończ sygnalizację przetwarzania
        }

        // Miganie diodą informacyjną - sygnał "system żywy"
        infoDiodeOn();
        _delay_ms(delayTime);   // Dioda włączona przez 600ms
        infoDiodeOff();
        _delay_ms(delayTime);   // Dioda wyłączona przez 600ms
    }
}
