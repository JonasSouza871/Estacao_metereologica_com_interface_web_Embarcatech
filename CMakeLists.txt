# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.0)
set(toolchainVersion 13_3_Rel1)
set(picotoolVersion 2.1.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)

if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

# Versão mínima do CMake requerida
cmake_minimum_required(VERSION 3.13)

# Configurações de padrões de linguagem
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Definir a placa como "pico_w" (caso esteja usando a Pico W, mesmo sem Wi-Fi)
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Inclui o SDK do Pico
include(pico_sdk_import.cmake)

# Nome do projeto
project(EstacaoMeteorologica_PicoW C CXX ASM)

# Inicializa o SDK do Pico
pico_sdk_init()

# Diretórios de inclusão para headers do projeto
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/lib/Display_Bibliotecas
    ${CMAKE_SOURCE_DIR}/lib/Matriz_Bibliotecas
    ${CMAKE_SOURCE_DIR}/lib/Wifi_Bibliotecas
)

# Define os arquivos do projeto
add_executable(EstacaoMeteorologica_PicoW
    main.c
    lib/Display_Bibliotecas/ssd1306.c
    lib/Matriz_Bibliotecas/matriz_led.c
    lib/aht20.c
    lib/bmp280.c
    lib/Wifi_Bibliotecas/lwipopts_examples_common.h
    lib/Wifi_Bibliotecas/lwipopts.h
    lib/html.c
)

target_link_libraries(EstacaoMeteorologica_PicoW
    pico_stdlib
    hardware_i2c
    hardware_pwm
    hardware_pio
    hardware_adc
    pico_cyw43_arch_lwip_threadsafe_background
)

# Habilita saída padrão via USB e UART
pico_enable_stdio_usb(EstacaoMeteorologica_PicoW 1)
pico_enable_stdio_uart(EstacaoMeteorologica_PicoW 1)

# Gera arquivos adicionais (binário, UF2, etc.)
pico_add_extra_outputs(EstacaoMeteorologica_PicoW)
