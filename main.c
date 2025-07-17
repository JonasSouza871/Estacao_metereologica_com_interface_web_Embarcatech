#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// Inclui os headers das suas bibliotecas de sensores
#include "aht20.h"
#include "bmp280.h"

// --- Definições de Hardware ---

// I2C para os Sensores (AHT20 e BMP280)
#define I2C_SENSORS_PORT i2c0
#define I2C_SENSORS_SDA_PIN 0
#define I2C_SENSORS_SCL_PIN 1

// I2C para o Display OLED (SSD1306)
// (Apenas inicializado aqui, não usado neste código simples)
#define I2C_DISPLAY_PORT i2c1
#define I2C_DISPLAY_SDA_PIN 14
#define I2C_DISPLAY_SCL_PIN 15

// Função para inicializar os periféricos I2C
void i2c_setup(void) {
    // Inicializa I2C0 para os sensores
    i2c_init(I2C_SENSORS_PORT, 100 * 1000); // 100kHz
    gpio_set_function(I2C_SENSORS_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SENSORS_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SENSORS_SDA_PIN);
    gpio_pull_up(I2C_SENSORS_SCL_PIN);

    // Inicializa I2C1 para o display
    i2c_init(I2C_DISPLAY_PORT, 400 * 1000); // 400kHz é comum para displays
    gpio_set_function(I2C_DISPLAY_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_DISPLAY_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_DISPLAY_SDA_PIN);
    gpio_pull_up(I2C_DISPLAY_SCL_PIN);
}

int main() {
    // Inicializa a saída padrão (USB/UART) para podermos ver os prints
    stdio_init_all();
    sleep_ms(2000); // Uma pequena pausa para o terminal serial conectar

    printf("Iniciando Estacao Meteorologica...\n");

    // Configura as portas I2C
    i2c_setup();

    // --- Inicialização dos Sensores ---

    // Inicializa o sensor AHT20
    if (aht20_init(I2C_SENSORS_PORT)) {
        printf("AHT20 inicializado com sucesso.\n");
    } else {
        printf("ERRO: Falha ao inicializar AHT20.\n");
        while (1); // Trava se não conseguir inicializar
    }

    // Inicializa o sensor BMP280
    bmp280_init(I2C_SENSORS_PORT);
    // Estrutura para guardar os dados de calibração do BMP280
    struct bmp280_calib_param bmp280_params;
    bmp280_get_calib_params(I2C_SENSORS_PORT, &bmp280_params);
    printf("BMP280 inicializado e calibrado.\n\n");

    // Loop principal para leitura contínua
    while (1) {
        // Variáveis para armazenar as temperaturas
        float temp_aht20 = 0.0;
        float temp_bmp280 = 0.0;

        // --- Leitura do AHT20 ---
        AHT20_Data aht20_data;
        if (aht20_read(I2C_SENSORS_PORT, &aht20_data)) {
            temp_aht20 = aht20_data.temperature; // Armazena a temperatura do AHT20
            printf("AHT20 -> Temp: %.2f C, Umidade: %.2f %%\n", temp_aht20, aht20_data.humidity);
        } else {
            printf("Falha ao ler dados do AHT20.\n");
        }

        // --- Leitura do BMP280 ---
        int32_t raw_temp, raw_pressure;
        bmp280_read_raw(I2C_SENSORS_PORT, &raw_temp, &raw_pressure);

        // Converte os valores brutos para valores legíveis
        int32_t compensated_temp_int = bmp280_convert_temp(raw_temp, &bmp280_params);
        int32_t compensated_pressure = bmp280_convert_pressure(raw_pressure, raw_temp, &bmp280_params);
        
        temp_bmp280 = compensated_temp_int / 100.0; // Armazena a temperatura do BMP280
        printf("BMP280 -> Temp: %.2f C, Pressao: %.2f hPa\n", temp_bmp280, compensated_pressure / 100.0);
        
        // --- Cálculo e Impressão da Média ---
        float temp_media = (temp_aht20 + temp_bmp280) / 2.0;
        printf("\n=> TEMPERATURA MEDIA: %.2f C\n", temp_media);
        
        printf("----------------------------------------\n");

        // Aguarda 2 segundos antes da próxima leitura
        sleep_ms(2000);
    }

    return 0;
}
