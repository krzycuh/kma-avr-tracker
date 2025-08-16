cmake_minimum_required(VERSION 3.20)
project(kma-avr-tracker C)

# Ustaw swój MCU i częstotliwość (dopasuj do fuse'ów)
set(MCU attiny2313 CACHE STRING "AVR MCU type")
set(F_CPU_HZ 8000000UL CACHE STRING "CPU frequency in Hz")

# Główny plik źródłowy
add_executable(attiny2313_fw
    attiny-rpi-2way-com.c
)

# Flagi kompilatora: ważne -mmcu, optymalizacje i standard
target_compile_options(attiny2313_fw PRIVATE
    -mmcu=${MCU}
    -Os
    -std=gnu11
    -Wall -Wextra
)

# Definicje preprocesora dla avr-libc (F_CPU)
target_compile_definitions(attiny2313_fw PRIVATE
    F_CPU=${F_CPU_HZ}
)

# Linker również musi znać -mmcu
target_link_options(attiny2313_fw PRIVATE
    -mmcu=${MCU}
)

# Artefakty po kompilacji: raport rozmiaru i plik .hex
# ${AVR_SIZE} i ${AVR_OBJCOPY} są wykrywane w toolchainie (cmake/avr-gcc-toolchain.cmake)
add_custom_command(TARGET attiny2313_fw POST_BUILD
    COMMAND ${AVR_SIZE} -C --mcu=${MCU} $<TARGET_FILE:attiny2313_fw>
    COMMAND ${AVR_OBJCOPY} -O ihex -R .eeprom
            $<TARGET_FILE:attiny2313_fw>
            $<TARGET_FILE_DIR:attiny2313_fw>/$<TARGET_NAME:attiny2313_fw>.hex
    COMMENT "Tworzenie pliku .hex i raport rozmiaru"
)
