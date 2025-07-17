#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
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
#define BOTAO_B_PIN             6 // Pino do botão B (GPIO 6)

// --- Definições de Estado ---
#define TELA_ABERTURA           0
#define TELA_VALORES            1

// --- Definições para Debouncing ---
#define DEBOUNCE_MS             250 // Tempo de debounce em milissegundos
#define INTERVALO_SENSORES_MS   2000 // Intervalo para leitura dos sensores
#define INTERVALO_TELA_MS       100 // Intervalo para atualização da tela

// --- Variáveis Globais ---
volatile uint8_t tela_atual = TELA_ABERTURA; // Controla a tela exibida
ssd1306_t tela_global; // Estrutura global para o display
float global_temp_aht20 = 0.0, global_temp_bmp280 = 0.0, global_temp_media = 0.0;
float global_umidade = 0.0, global_pressao = 0.0; // Valores dos sensores

// --- Protótipos das Funções ---
void configurar_perifericos(ssd1306_t *tela, struct bmp280_calib_param *params_calibracao);
void configurar_botoes(void);
void mostrar_tela_abertura(ssd1306_t *tela);
void mostrar_tela_valores(ssd1306_t *tela, float temp_aht20, float temp_bmp280, float temp_media, float umidade, float pressao);
void atualizar_tela(ssd1306_t *tela, float temp_aht20, float temp_bmp280, float temp_media, float umidade, float pressao);
void ler_e_exibir_dados(struct bmp280_calib_param *params_calibracao, float *temp_aht20, float *temp_bmp280, float *temp_media, float *umidade, float *pressao);
void botoes_callback(uint gpio, uint32_t events);

// --- Função Principal ---
int main() {
    stdio_init_all();
    sleep_ms(2000); // Pausa para conexão do monitor serial

    struct bmp280_calib_param params_calibracao_bmp;
    uint64_t ultimo_tempo_sensores = 0;

    configurar_perifericos(&tela_global, &params_calibracao_bmp);
    configurar_botoes(); // Configura os botões A e B
    mostrar_tela_abertura(&tela_global);
    sleep_ms(2500); // Tempo de exibição da tela de abertura

    while (1) {
        uint64_t tempo_atual = time_us_64();

        // Lê sensores a cada INTERVALO_SENSORES_MS
        if ((tempo_atual - ultimo_tempo_sensores) >= (INTERVALO_SENSORES_MS * 1000)) {
            ler_e_exibir_dados(&params_calibracao_bmp, &global_temp_aht20, &global_temp_bmp280, &global_temp_media, &global_umidade, &global_pressao);
            ultimo_tempo_sensores = tempo_atual;
        }

        // Atualiza a tela a cada INTERVALO_TELA_MS
        atualizar_tela(&tela_global, global_temp_aht20, global_temp_bmp280, global_temp_media, global_umidade, global_pressao);
        sleep_ms(INTERVALO_TELA_MS);
    }

    return 0;
}

// Inicializa todos os periféricos de hardware: I2C, Display, Sensores.
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

// Configura os botões A e B com interrupções de alta prioridade e debouncing.
void configurar_botoes(void) {
    // Configura Botão A
    gpio_init(BOTAO_A_PIN);
    gpio_set_dir(BOTAO_A_PIN, GPIO_IN);
    gpio_pull_up(BOTAO_A_PIN); // Pull-up interno para evitar flutuação

    // Configura Botão B
    gpio_init(BOTAO_B_PIN);
    gpio_set_dir(BOTAO_B_PIN, GPIO_IN);
    gpio_pull_up(BOTAO_B_PIN); // Pull-up interno para evitar flutuação

    // Habilita interrupções para ambos os botões com alta prioridade
    gpio_set_irq_enabled_with_callback(BOTAO_A_PIN, GPIO_IRQ_EDGE_FALL, true, &botoes_callback);
    gpio_set_irq_enabled_with_callback(BOTAO_B_PIN, GPIO_IRQ_EDGE_FALL, true, &botoes_callback);

    // Define alta prioridade para as interrupções de GPIO
    irq_set_priority(IO_IRQ_BANK0, 0x00); // 0x00 é a maior prioridade
}

// Callback para interrupções dos botões A e B com debouncing.
void botoes_callback(uint gpio, uint32_t events) {
    static uint64_t ultimo_tempo_a = 0;
    static uint64_t ultimo_tempo_b = 0;
    uint64_t tempo_atual = time_us_64();

    if (events & GPIO_IRQ_EDGE_FALL) {
        if (gpio == BOTAO_A_PIN && (tempo_atual - ultimo_tempo_a) > (DEBOUNCE_MS * 1000)) {
            tela_atual = TELA_VALORES; // Avança para a tela de valores
            ultimo_tempo_a = tempo_atual;
            atualizar_tela(&tela_global, global_temp_aht20, global_temp_bmp280, global_temp_media, global_umidade, global_pressao);
        } else if (gpio == BOTAO_B_PIN && (tempo_atual - ultimo_tempo_b) > (DEBOUNCE_MS * 1000)) {
            if (tela_atual == TELA_ABERTURA) {
                tela_atual = TELA_VALORES; // Vai para a última tela se estiver na abertura
            } else {
                tela_atual = TELA_ABERTURA; // Volta para a tela anterior
            }
            ultimo_tempo_b = tempo_atual;
            atualizar_tela(&tela_global, global_temp_aht20, global_temp_bmp280, global_temp_media, global_umidade, global_pressao);
        }
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

// Exibe a tela com os valores medidos pelos sensores.
void mostrar_tela_valores(ssd1306_t *tela, float temp_aht20, float temp_bmp280, float temp_media, float umidade, float pressao) {
    ssd1306_fill(tela, 0); // Preenche a tela com fundo preto

    // Título centralizado
    const char *titulo = "Valores medidos:";
    uint8_t x_titulo = (TELA_LARGURA - (strlen(titulo) * 8)) / 2;
    ssd1306_draw_string(tela, titulo, x_titulo, 0, false);

    // Formata os valores para exibição
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "TempAHT: %.2fC", temp_aht20);
    ssd1306_draw_string(tela, buffer, 0, 10, false);

    snprintf(buffer, sizeof(buffer), "TempBMP: %.2fC", temp_bmp280);
    ssd1306_draw_string(tela, buffer, 0, 20, false);

    snprintf(buffer, sizeof(buffer), "TempMed: %.2fC", temp_media);
    ssd1306_draw_string(tela, buffer, 0, 30, false);

    snprintf(buffer, sizeof(buffer), "Umidade: %.2f%%", umidade);
    ssd1306_draw_string(tela, buffer, 0, 40, false);

    snprintf(buffer, sizeof(buffer), "Press: %.2fhPa", pressao);
    ssd1306_draw_string(tela, buffer, 0, 50, false);

    ssd1306_send_data(tela); // Envia os dados para o display
}

// Atualiza o conteúdo do display com base no estado da tela_atual.
void atualizar_tela(ssd1306_t *tela, float temp_aht20, float temp_bmp280, float temp_media, float umidade, float pressao) {
    switch (tela_atual) {
        case TELA_ABERTURA:
            mostrar_tela_abertura(tela);
            break;
        case TELA_VALORES:
            mostrar_tela_valores(tela, temp_aht20, temp_bmp280, temp_media, umidade, pressao);
            break;
        default:
            mostrar_tela_abertura(tela); // Fallback para tela de abertura
            break;
    }
}

// Lê os dados dos sensores, processa e exibe no terminal serial.
void ler_e_exibir_dados(struct bmp280_calib_param *params_calibracao, float *temp_aht20, float *temp_bmp280, float *temp_media, float *umidade, float *pressao) {
    AHT20_Data dados_aht20;
    if (aht20_read(I2C_SENSORES_PORT, &dados_aht20)) {
        *temp_aht20 = dados_aht20.temperature;
        *umidade = dados_aht20.humidity;
    } else {
        printf("Falha ao ler dados do AHT20.\n");
        *temp_aht20 = 0.0;
        *umidade = 0.0;
    }

    int32_t temp_bruta, pressao_bruta;
    bmp280_read_raw(I2C_SENSORES_PORT, &temp_bruta, &pressao_bruta);
    
    int32_t temp_compensada_int = bmp280_convert_temp(temp_bruta, params_calibracao);
    int32_t pressao_compensada_int = bmp280_convert_pressure(pressao_bruta, temp_bruta, params_calibracao);
    
    *temp_bmp280 = temp_compensada_int / 100.0;
    *pressao = pressao_compensada_int / 100.0;
    
    *temp_media = (*temp_aht20 + *temp_bmp280) / 2.0;
    
    // Exibe todos os dados de forma consolidada no terminal
    printf("Temp Media: %.2f C | Umidade: %.2f %% | Pressao: %.2f hPa\n",
           *temp_media, *umidade, *pressao);
    printf("----------------------------------------------------------------\n");
}