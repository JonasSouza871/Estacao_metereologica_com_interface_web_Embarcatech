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
#define I2C_SENSORES_PORT       i2c0    // Barramento I2C 0 para os sensores (AHT20, BMP280)
#define I2C_SENSORES_SDA_PIN    0       // Pino GPIO 0 para o I2C Data (SDA) dos sensores
#define I2C_SENSORES_SCL_PIN    1       // Pino GPIO 1 para o I2C Clock (SCL) dos sensores
#define I2C_TELA_PORT           i2c1    // Barramento I2C 1 para o display OLED
#define I2C_TELA_SDA_PIN        14      // Pino GPIO 14 para o I2C Data (SDA) do display
#define I2C_TELA_SCL_PIN        15      // Pino GPIO 15 para o I2C Clock (SCL) do display
#define TELA_ENDERECO           0x3C    // Endereço I2C do display SSD1306
#define TELA_LARGURA            128     // Largura do display em pixels
#define TELA_ALTURA             64      // Altura do display em pixels
#define BOTAO_A_PIN             5       // Pino GPIO 5 para o botão A (Avançar)
#define BOTAO_B_PIN             6       // Pino GPIO 6 para o botão B (Voltar)

// --- Definições de Estado e Tempo ---
#define TELA_ABERTURA           0
#define TELA_VALORES            1
#define TELA_CONEXAO            2       // Nova tela de conexão
#define DEBOUNCE_MS             250     // Filtro para evitar múltiplas leituras do botão
#define INTERVALO_LEITURA_MS    2000    // Frequência de leitura dos sensores

// --- Variáveis Globais ---
volatile uint8_t tela_atual = TELA_ABERTURA;
volatile bool precisa_atualizar_tela = true;

// --- Protótipos das Funções ---
void configurar_perifericos(ssd1306_t *tela, struct bmp280_calib_param *params_calibracao);
void configurar_botoes(void);
void mostrar_tela_abertura(ssd1306_t *tela);
void mostrar_tela_valores(ssd1306_t *tela, float temp_aht20, float temp_bmp280, float temp_media, float umidade, float pressao);
void mostrar_tela_conexao(ssd1306_t *tela); // Protótipo da nova função
void atualizar_tela(ssd1306_t *tela, float temp_aht20, float temp_bmp280, float temp_media, float umidade, float pressao);
void ler_e_exibir_dados(struct bmp280_calib_param *params_calibracao, float *temp_aht20, float *temp_bmp280, float *temp_media, float *umidade, float *pressao);
void botoes_callback(uint gpio, uint32_t events);

// --- Função Principal ---
int main() {
    stdio_init_all();
    sleep_ms(2000); // Pausa para inicializar o monitor serial

    ssd1306_t tela;
    struct bmp280_calib_param params_calibracao_bmp;
    float temp_aht20 = 0.0, temp_bmp280 = 0.0, temp_media = 0.0, umidade = 0.0, pressao = 0.0;
    uint64_t proxima_leitura_sensores = 0;

    configurar_perifericos(&tela, &params_calibracao_bmp);
    configurar_botoes();
    
    mostrar_tela_abertura(&tela);
    sleep_ms(2500);
    precisa_atualizar_tela = false; // Evita atualização automática após a tela de abertura

    while (1) {
        if (time_us_64() >= proxima_leitura_sensores) {
            ler_e_exibir_dados(&params_calibracao_bmp, &temp_aht20, &temp_bmp280, &temp_media, &umidade, &pressao);
            proxima_leitura_sensores = time_us_64() + (INTERVALO_LEITURA_MS * 1000);
            
            if (tela_atual == TELA_VALORES) {
                precisa_atualizar_tela = true; // Marca para redesenhar apenas a tela de valores
            }
        }

        if (precisa_atualizar_tela) {
            atualizar_tela(&tela, temp_aht20, temp_bmp280, temp_media, umidade, pressao);
            precisa_atualizar_tela = false; // Reseta a flag após a atualização
        }
        sleep_ms(10); // Pequena pausa para não sobrecarregar a CPU
    }
    return 0;
}

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

void configurar_botoes(void) {
    gpio_init(BOTAO_A_PIN);
    gpio_set_dir(BOTAO_A_PIN, GPIO_IN);
    gpio_pull_up(BOTAO_A_PIN);

    gpio_init(BOTAO_B_PIN);
    gpio_set_dir(BOTAO_B_PIN, GPIO_IN);
    gpio_pull_up(BOTAO_B_PIN);

    gpio_set_irq_enabled_with_callback(BOTAO_A_PIN, GPIO_IRQ_EDGE_FALL, true, &botoes_callback);
    gpio_set_irq_enabled_with_callback(BOTAO_B_PIN, GPIO_IRQ_EDGE_FALL, true, &botoes_callback);
    irq_set_priority(IO_IRQ_BANK0, 0x00); // Define a interrupção dos botões como a mais alta prioridade
}

void botoes_callback(uint gpio, uint32_t events) {
    static uint64_t ultimo_tempo_a = 0;
    static uint64_t ultimo_tempo_b = 0;
    uint64_t tempo_atual = time_us_64();

    if (events & GPIO_IRQ_EDGE_FALL) { // A interrupção é ativada na borda de descida (botão pressionado)
        if (gpio == BOTAO_A_PIN && (tempo_atual - ultimo_tempo_a) > (DEBOUNCE_MS * 1000)) {
            // Botão A avança para a próxima tela
            if (tela_atual == TELA_ABERTURA) tela_atual = TELA_VALORES;
            else if (tela_atual == TELA_VALORES) tela_atual = TELA_CONEXAO;
            else if (tela_atual == TELA_CONEXAO) tela_atual = TELA_ABERTURA; // Volta para o início
            
            precisa_atualizar_tela = true;
            ultimo_tempo_a = tempo_atual;
        } else if (gpio == BOTAO_B_PIN && (tempo_atual - ultimo_tempo_b) > (DEBOUNCE_MS * 1000)) {
            // Botão B volta para a tela anterior
            if (tela_atual == TELA_CONEXAO) tela_atual = TELA_VALORES;
            else if (tela_atual == TELA_VALORES) tela_atual = TELA_ABERTURA;
            else if (tela_atual == TELA_ABERTURA) tela_atual = TELA_CONEXAO; // Volta para a última
            
            precisa_atualizar_tela = true;
            ultimo_tempo_b = tempo_atual;
        }
    }
}

void mostrar_tela_abertura(ssd1306_t *tela) {
    ssd1306_fill(tela, 0);
    ssd1306_rect(tela, 5, 5, 118, 54, 1, false);

    const char *nome_projeto = "PicoAtmos";
    uint8_t x_inicial = (TELA_LARGURA - (strlen(nome_projeto) * 8)) / 2; // Centraliza o texto
    ssd1306_draw_string(tela, nome_projeto, x_inicial, 28, false);

    const char *subtitulo = "Pronto!";
    uint8_t x_subtitulo = (TELA_LARGURA - (strlen(subtitulo) * 6)) / 2; // Centraliza o texto
    ssd1306_draw_string(tela, subtitulo, x_subtitulo, 45, true);

    ssd1306_send_data(tela);
}

void mostrar_tela_valores(ssd1306_t *tela, float temp_aht20, float temp_bmp280, float temp_media, float umidade, float pressao) {
    ssd1306_fill(tela, 0);
    char buffer[32];

    const char *titulo = "Valores Medidos";
    uint8_t x_titulo = (TELA_LARGURA - (strlen(titulo) * 8)) / 2;
    ssd1306_draw_string(tela, titulo, x_titulo, 0, false);

    snprintf(buffer, sizeof(buffer), "TempAHT: %.1fC", temp_aht20);
    ssd1306_draw_string(tela, buffer, 0, 12, false);

    snprintf(buffer, sizeof(buffer), "TempBMP: %.1fC", temp_bmp280);
    ssd1306_draw_string(tela, buffer, 0, 22, false);

    snprintf(buffer, sizeof(buffer), "TempMed: %.1fC", temp_media);
    ssd1306_draw_string(tela, buffer, 0, 32, false);

    snprintf(buffer, sizeof(buffer), "Umidade: %.1f%%", umidade);
    ssd1306_draw_string(tela, buffer, 0, 42, false);

    snprintf(buffer, sizeof(buffer), "Pressao: %.1fhPa", pressao / 100.0); // Converte de Pa para hPa
    ssd1306_draw_string(tela, buffer, 0, 52, false);

    ssd1306_send_data(tela);
}

// Nova função para desenhar a tela de conexão
void mostrar_tela_conexao(ssd1306_t *tela) {
    ssd1306_fill(tela, 0);

    const char *titulo = "Conexao";
    uint8_t x_titulo = (TELA_LARGURA - (strlen(titulo) * 8)) / 2; // Centraliza o título
    ssd1306_draw_string(tela, titulo, x_titulo, 0, false);

    ssd1306_draw_string(tela, "TempSet:", 0, 10, false);
    ssd1306_draw_string(tela, "UmidSet:", 0, 20, false);
    ssd1306_draw_string(tela, "PresSet:", 0, 30, false);
    ssd1306_draw_string(tela, "IP:", 0, 40, false);
    ssd1306_draw_string(tela, "Status:", 0, 50, false);
    // A linha "TempoRede" foi omitida por falta de espaço vertical (6 linhas já ocupam 60 pixels)

    ssd1306_send_data(tela);
}

void atualizar_tela(ssd1306_t *tela, float temp_aht20, float temp_bmp280, float temp_media, float umidade, float pressao) {
    switch (tela_atual) {
        case TELA_ABERTURA:
            mostrar_tela_abertura(tela);
            break;
        case TELA_VALORES:
            mostrar_tela_valores(tela, temp_aht20, temp_bmp280, temp_media, umidade, pressao);
            break;
        case TELA_CONEXAO: // Adiciona o caso para a nova tela
            mostrar_tela_conexao(tela);
            break;
        default:
            mostrar_tela_abertura(tela); // Tela padrão em caso de estado inválido
            break;
    }
}

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
    *pressao = pressao_compensada_int; // Armazena o valor em Pascal (Pa)
    *temp_media = (*temp_aht20 + *temp_bmp280) / 2.0;
    
    printf("Temp Media: %.2f C | Umidade: %.2f %% | Pressao: %.2f hPa\n",
           *temp_media, *umidade, *pressao / 100.0);
    printf("----------------------------------------------------------------\n");
}
