#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
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

#define BOTAO_A_PIN             5 // Pino do botão A (GPIO 5)

// --- Definições de Estado ---
#define TELA_ABERTURA           0
#define TELA_VAZIA              1

// --- Variáveis Globais ---
volatile uint8_t tela_atual = TELA_ABERTURA; // Controla a tela exibida

// --- Protótipos das Funções ---
void configurar_perifericos(ssd1306_t *tela, struct bmp280_calib_param *params_calibracao);
void configurar_botao(void);
void mostrar_tela_abertura(ssd1306_t *tela);
void mostrar_tela_vazia(ssd1306_t *tela);
void atualizar_tela(ssd1306_t *tela);
void ler_e_exibir_dados(struct bmp280_calib_param *params_calibracao);
void botao_a_callback(uint gpio, uint32_t events);

// --- Função Principal ---
int main() {
    stdio_init_all();
    sleep_ms(2000); // Pausa para conexão do monitor serial

    ssd1306_t tela;
    struct bmp280_calib_param params_calibracao_bmp;

    configurar_perifericos(&tela, &params_calibracao_bmp);
    configurar_botao(); // Configura o botão A
    mostrar_tela_abertura(&tela);
    sleep_ms(2500); // Tempo de exibição da tela de abertura

    while (1) {
        if (tela_atual == TELA_ABERTURA) {
            ler_e_exibir_dados(&params_calibracao_bmp);
        }
        atualizar_tela(&tela); // Atualiza a tela com base no estado
        sleep_ms(2000);
    }

    return 0;
}

// Inicializa todos os periféricos de hardware: I2C, Display, Sensores e Botão.
void configurar_perifericos(ssd1306_t *tela, struct bmp280_calib_param *params_calibracao) {
    printf("Iniciando PicoAtmos..\n");

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
    ssd1306_config(tela);

    if (!aht20_init(I2C_SENSORES_PORT)) {
        printf("ERRO: Falha ao inicializar AHT20.\n");
        while (1); // Trava o programa em caso de falha crítica
    }
    printf("AHT20 inicializado com sucesso.\n");

    bmp280_init(I2C_SENSORES_PORT);
    bmp280_get_calib_params(I2C_SENSORES_PORT, params_calibracao);
    printf("BMP280 inicializado e calibrado.\n\n");
}

// Configura o botão A com interrupção para alternar telas.
void configurar_botao(void) {
    gpio_init(BOTAO_A_PIN);
    gpio_set_dir(BOTAO_A_PIN, GPIO_IN);
    gpio_pull_up(BOTAO_A_PIN); // Pull-up interno para evitar flutuação
    gpio_set_irq_enabled_with_callback(BOTAO_A_PIN, GPIO_IRQ_EDGE_FALL, true, &botao_a_callback);
}

// Callback para interrupção do botão A.
void botao_a_callback(uint gpio, uint32_t events) {
    if (gpio == BOTAO_A_PIN && events & GPIO_IRQ_EDGE_FALL) {
        tela_atual = TELA_VAZIA; // Altera para a tela vazia
    }
}

// Mostra uma tela de abertura ("splash screen") fixa no display.
void mostrar_tela_abertura(ssd1306_t *tela) {
    ssd1306_fill(tela, 0);
    ssd1306_rect(tela, 5, 5, 118, 54, 1, false);

    const char *nome_projeto = "PicoAtmos";
    uint8_t x_inicial = (TELA_LARGURA - (strlen(nome_projeto) * 8)) / 2;
    uint8_t y_inicial = 28;
    ssd1306_draw_string(tela, nome_projeto, x_inicial, y_inicial, false);

    const char *subtitulo = "Iniciando..";
    uint8_t x_subtitulo = (TELA_LARGURA - (strlen(subtitulo) * 6)) / 2;
    ssd1306_draw_string(tela, subtitulo, x_subtitulo, 45, true);

    ssd1306_send_data(tela);
}

// Exibe uma tela vazia no display.
void mostrar_tela_vazia(ssd1306_t *tela) {
    ssd1306_fill(tela, 0); // Preenche a tela com fundo preto
    ssd1306_send_data(tela); // Envia os dados para o display
}

// Atualiza o conteúdo do display com base no estado da tela_atual.
void atualizar_tela(ssd1306_t *tela) {
    switch (tela_atual) {
        case TELA_ABERTURA:
            mostrar_tela_abertura(tela);
            break;
        case TELA_VAZIA:
            mostrar_tela_vazia(tela);
            break;
        default:
            mostrar_tela_abertura(tela); // Fallback para tela de abertura
            break;
    }
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