#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "aht20.h"
#include "bmp280.h"
#include "ssd1306.h"

// --- Definições de Hardware ---
#define I2C_SENSORES_PORT       i2c0
#define I2C_SENSORES_SDA_PIN    0
#define I2C_SENSORES_SCL_PIN    1

#define I2C_TELA_PORT           i2c1
#define I2C_TELA_SDA_PIN        14
#define I2C_TELA_SCL_PIN        15

#define TELA_ENDERECO           0x3C
#define TELA_LARGURA            128
#define TELA_ALTURA             64

// --- Protótipos das Funções ---
void configurar_perifericos(ssd1306_t *tela, struct bmp280_calib_param *params_calibracao);
void mostrar_tela_abertura(ssd1306_t *tela);
void ler_e_exibir_dados(struct bmp280_calib_param *params_calibracao);

// --- Função Principal ---
int main() {
    stdio_init_all();
    sleep_ms(2000); // Pausa para conexão do monitor serial

    ssd1306_t tela;
    struct bmp280_calib_param params_calibracao_bmp;

    configurar_perifericos(&tela, &params_calibracao_bmp);
    mostrar_tela_abertura(&tela);
    sleep_ms(2500); // Tempo de exibição da tela de abertura

    while (1) {
        ler_e_exibir_dados(&params_calibracao_bmp);
        sleep_ms(2000);
    }
    return 0;
}

// Inicializa todos os periféricos de hardware: I2C, Display e Sensores.
void configurar_perifericos(ssd1306_t *tela, struct bmp280_calib_param *params_calibracao) {
    printf("Iniciando PicoAtmos...\n");

    i2c_init(I2C_SENSORES_PORT, 100 * 1000);
    gpio_set_function(I2C_SENSORES_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SENSORES_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SENSORES_SDA_PIN);
    gpio_pull_up(I2C_SENSORES_SCL_PIN);

    i2c_init(I2C_TELA_PORT, 400 * 1000);
    gpio_set_function(I2C_TELA_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_TELA_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_TELA_SDA_PIN);
    gpio_pull_up(I2C_TELA_SCL_PIN);

    ssd1306_init(tela, TELA_LARGURA, TELA_ALTURA, false, TELA_ENDERECO, I2C_TELA_PORT);
    ssd1306_config(tela); // Added to configure the display

    if (!aht20_init(I2C_SENSORES_PORT)) {
        printf("ERRO: Falha ao inicializar AHT20.\n");
        while (1); // Trava o programa em caso de falha crítica
    }
    printf("AHT20 inicializado com sucesso.\n");

    bmp280_init(I2C_SENSORES_PORT);
    bmp280_get_calib_params(I2C_SENSORES_PORT, params_calibracao);
    printf("BMP280 inicializado e calibrado.\n\n");
}

// Mostra uma tela de abertura ("splash screen") fixa no display.
void mostrar_tela_abertura(ssd1306_t *tela) {
    ssd1306_fill(tela, 0);
    ssd1306_rect(tela, 5, 5, 118, 54, 1, false);

    const char *nome_projeto = "PicoAtmos";
    uint8_t x_inicial = (TELA_LARGURA - (strlen(nome_projeto) * 8)) / 2;
    uint8_t y_inicial = 28;
    ssd1306_draw_string(tela, nome_projeto, x_inicial, y_inicial, false);

    const char *subtitulo = "Iniciando...";
    uint8_t x_subtitulo = (TELA_LARGURA - (strlen(subtitulo) * 6)) / 2;
    ssd1306_draw_string(tela, subtitulo, x_subtitulo, 45, true);

    ssd1306_send_data(tela);
}

// Lê os dados dos sensores, processa e exibe no terminal serial.
void ler_e_exibir_dados(struct bmp280_calib_param *params_calibracao) {
    float temperatura_aht20 = 0.0, umidade_aht20 = 0.0;
    float temperatura_bmp280 = 0.0, pressao_hpa = 0.0;

    AHT20_Data dados_aht20;
    if (aht20_read(I2C_SENSORES_PORT, &dados_aht20)) {
        temperatura_aht20 = dados_aht20.temperature;
        umidade_aht20 = dados_aht20.humidity;
    } else {
        printf("Falha ao ler dados do AHT20.\n");
    }

    int32_t temp_bruta, pressao_bruta;
    bmp280_read_raw(I2C_SENSORES_PORT, &temp_bruta, &pressao_bruta);
    
    int32_t temp_compensada_int = bmp280_convert_temp(temp_bruta, params_calibracao);
    int32_t pressao_compensada_int = bmp280_convert_pressure(pressao_bruta, temp_bruta, params_calibracao);
    
    temperatura_bmp280 = temp_compensada_int / 100.0;
    pressao_hpa = pressao_compensada_int / 100.0;
    
    float temperatura_media = (temperatura_aht20 + temperatura_bmp280) / 2.0;
    
    // Exibe todos os dados de forma consolidada no terminal
    printf("Temp Media: %.2f C | Umidade: %.2f %% | Pressao: %.2f hPa\n", 
           temperatura_media, umidade_aht20, pressao_hpa);
    printf("----------------------------------------------------------------\n");
}