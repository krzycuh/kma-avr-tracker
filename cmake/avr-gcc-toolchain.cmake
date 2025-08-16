# cmake/avr-gcc-toolchain.cmake
# Toolchain CMake dla AVR (avr-gcc), zapewniający poprawne rozpoznawanie przez CLion/clangd.
# Użyj go w CLion: Settings/Preferences > Build, Execution, Deployment > Toolchains > CMake > Toolchain file.

# Informujemy CMake, że kompilujemy dla systemu "Generic" (cross-compilation)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Szukamy narzędzi w PATH (WSL, Linux, macOS lub Windows z dodanym Toolchainem do PATH)
find_program(AVR_CC avr-gcc)
find_program(AVR_CXX avr-g++)
find_program(AVR_AR avr-ar)
find_program(AVR_OBJCOPY avr-objcopy)
find_program(AVR_OBJDUMP avr-objdump)
find_program(AVR_SIZE avr-size)

if(NOT AVR_CC)
  message(FATAL_ERROR "avr-gcc nie został znaleziony w PATH. Upewnij się, że AVR Toolchain jest zainstalowany i dostępny.")
endif()

# Ustawiamy kompilatory i narzędzia dla CMake
set(CMAKE_C_COMPILER   ${AVR_CC})
set(CMAKE_CXX_COMPILER ${AVR_CXX})
set(CMAKE_AR           ${AVR_AR})

# Zapisujemy ścieżki do dodatkowych narzędzi (możesz ich użyć w custom_command)
set(AVR_OBJCOPY ${AVR_OBJCOPY})
set(AVR_OBJDUMP ${AVR_OBJDUMP})
set(AVR_SIZE    ${AVR_SIZE})

# Nie próbuj linkować z bibliotekami hosta przy testach
set(CMAKE_EXE_LINKER_FLAGS_INIT "")

# Opcjonalnie, można doprecyzować tryb wyszukiwania:
# set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
# set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
