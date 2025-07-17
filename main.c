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

// --- Definições para Lógica de Tempo ---
#define DEBOUNCE_MS             250      // Tempo de debounce em milissegundos
#define INTERVALO_LEITURA_MS    2000    // Intervalo para ler os sensores

// --- Variáveis Globais ---
volatile uint8_t tela_atual = TELA_ABERTURA; // Controla a tela exibida
volatile bool precisa_atualizar_tela = true; // Flag para forçar a atualização da tela

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

    ssd1306_t tela;
    struct bmp280_calib_param params_calibracao_bmp;
    float temp_aht20 = 0.0, temp_bmp280 = 0.0, temp_media = 0.0, umidade = 0.0, pressao = 0.0;
    
    // Variável para controlar o tempo da próxima leitura dos sensores
    uint64_t proxima_leitura_sensores = 0;

    configurar_perifericos(&tela, &params_calibracao_bmp);
    configurar_botoes();
    
    // A tela de abertura é mostrada uma vez aqui.
    // O estado inicial já é TELA_ABERTURA.
    mostrar_tela_abertura(&tela);
    sleep_ms(2500); // Mantém a tela de abertura por um tempo inicial.

    // **CORREÇÃO**: Reseta a flag para evitar uma atualização indesejada no primeiro loop.
    // O programa agora vai esperar a interação do usuário para mudar de tela.
    precisa_atualizar_tela = false;

    while (1) {
        // --- LÓGICA SEM BLOQUEIO (NON-BLOCKING) ---

        // 1. Verifica se já é hora de ler os sensores
        if (time_us_64() >= proxima_leitura_sensores) {
            ler_e_exibir_dados(&params_calibracao_bmp, &temp_aht20, &temp_bmp280, &temp_media, &umidade, &pressao);
            // Agenda a próxima leitura para daqui a INTERVALO_LEITURA_MS
            proxima_leitura_sensores = time_us_64() + (INTERVALO_LEITURA_MS * 1000);
            
            // **CORREÇÃO**: Só marca para atualizar a tela se já estivermos na tela de valores.
            // Isso evita que a tela de abertura seja redesenhada com dados novos.
            if (tela_atual == TELA_VALORES) {
                precisa_atualizar_tela = true;
            }
        }

        // 2. Verifica se a tela precisa ser atualizada (seja por nova leitura ou por botão)
        if (precisa_atualizar_tela) {
            atualizar_tela(&tela, temp_aht20, temp_bmp280, temp_media, umidade, pressao);
            precisa_atualizar_tela = false; // Reseta a flag após atualizar
        }

        // Adiciona uma pequena pausa para não sobrecarregar a CPU.
        sleep_ms(10);
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
    gpio_pull_up(BOTAO_A_PIN);
    // Configura Botão B
    gpio_init(BOTAO_B_PIN);
    gpio_set_dir(BOTAO_B_PIN, GPIO_IN);
    gpio_pull_up(BOTAO_B_PIN);

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
            tela_atual = TELA_VALORES;
            precisa_atualizar_tela = true; // Força a atualização da tela
            ultimo_tempo_a = tempo_atual;
        } else if (gpio == BOTAO_B_PIN && (tempo_atual - ultimo_tempo_b) > (DEBOUNCE_MS * 1000)) {
            // Alterna entre as telas
            if (tela_atual == TELA_ABERTURA) {
                tela_atual = TELA_VALORES;
            } else {
                tela_atual = TELA_ABERTURA;
            }
            precisa_atualizar_tela = true; // Força a atualização da tela
            ultimo_tempo_b = tempo_atual;
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

    const char *subtitulo = "Pronto!";
    uint8_t x_subtitulo = (TELA_LARGURA - (strlen(subtitulo) * 6)) / 2;
    ssd1306_draw_string(tela, subtitulo, x_subtitulo, 45, true);

    ssd1306_send_data(tela);
}

// Exibe a tela com os valores medidos pelos sensores.
void mostrar_tela_valores(ssd1306_t *tela, float temp_aht20, float temp_bmp280, float temp_media, float umidade, float pressao) {
    ssd1306_fill(tela, 0); // Preenche a tela com fundo preto

    // Título centralizado
    const char *titulo = "Valores Medidos";
    uint8_t x_titulo = (TELA_LARGURA - (strlen(titulo) * 8)) / 2;
    ssd1306_draw_string(tela, titulo, x_titulo, 0, false);

    // Formata os valores para exibição
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "TempAHT: %.1fC", temp_aht20);
    ssd1306_draw_string(tela, buffer, 0, 12, false);

    snprintf(buffer, sizeof(buffer), "TempBMP: %.1fC", temp_bmp280);
    ssd1306_draw_string(tela, buffer, 0, 22, false);

    snprintf(buffer, sizeof(buffer), "TempMed: %.1fC", temp_media);
    ssd1306_draw_string(tela, buffer, 0, 32, false);

    snprintf(buffer, sizeof(buffer), "Umidade: %.1f%%", umidade);
    ssd1306_draw_string(tela, buffer, 0, 42, false);

    snprintf(buffer, sizeof(buffer), "Pressao: %.1fhPa", pressao / 100.0);
    ssd1306_draw_string(tela, buffer, 0, 52, false);

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
    *pressao = pressao_compensada_int; // Mantém em Pa para conversão depois
    
    *temp_media = (*temp_aht20 + *temp_bmp280) / 2.0;
    
    // Exibe todos os dados de forma consolidada no terminal
    printf("Temp Media: %.2f C | Umidade: %.2f %% | Pressao: %.2f hPa\n",
           *temp_media, *umidade, *pressao / 100.0);
    printf("----------------------------------------------------------------\n");
}
