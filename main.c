#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/irq.h"
#include "aht20.h"
#include "bmp280.h"
#include "ssd1306.h"
#include "font.h"

/* ---- Configurações de Hardware -------------------------------- */
#define I2C_SENSORES_PORT       i2c0
#define I2C_SENSORES_SDA_PIN    0
#define I2C_SENSORES_SCL_PIN    1
#define I2C_TELA_PORT           i2c1
#define I2C_TELA_SDA_PIN        14
#define I2C_TELA_SCL_PIN        15
#define TELA_ENDERECO           0x3C
#define TELA_LARGURA            128
#define TELA_ALTURA             64
#define BOTAO_A_PIN             5
#define BOTAO_B_PIN             6
#define JOYSTICK_PIN            26

/* ---- Configurações de Estado e Tempo -------------------------- */
#define TELA_ABERTURA           0
#define TELA_VALORES            1
#define TELA_CONEXAO            2
#define TELA_GRAFICO            3
#define TELA_UMIDADE            4
#define TELA_PRESSAO            5 // NOVA TELA
#define NUM_TELAS               6 // NÚMERO TOTAL DE TELAS
#define DEBOUNCE_MS             250
#define INTERVALO_LEITURA_MS    2000
#define TAMANHO_GRAFICO         30 // 60 segundos / 2s por leitura

/* ---- Variáveis Globais ---------------------------------------- */
volatile uint8_t tela_atual = TELA_ABERTURA;
volatile bool    atualizar_tela_flag = true;
volatile float   fator_zoom = 1.0f;
volatile float   fator_zoom_umidade = 1.0f;
volatile float   fator_zoom_pressao = 1.0f; // NOVO FATOR DE ZOOM PARA PRESSÃO

static uint64_t  ultimo_zoom_ms = 0;
const  uint32_t  ZOOM_DEBOUNCE_MS = 120;

// Buffers para os gráficos
float buffer_temperaturas[TAMANHO_GRAFICO];
float buffer_umidade[TAMANHO_GRAFICO];
float buffer_pressao[TAMANHO_GRAFICO]; // NOVO BUFFER PARA PRESSÃO
int   indice_buffer = 0;
int   contador_buffer = 0;

/* ---- Protótipos de Funções ------------------------------------ */
void configurar_perifericos(ssd1306_t *, struct bmp280_calib_param *);
void configurar_botoes(void);
void configurar_joystick(void);
void mostrar_tela_abertura(ssd1306_t *);
void mostrar_tela_valores(ssd1306_t *, float, float, float, float, float);
void mostrar_tela_conexao(ssd1306_t *);
void mostrar_tela_grafico(ssd1306_t *);
void mostrar_tela_umidade(ssd1306_t *);
void mostrar_tela_pressao(ssd1306_t *); // NOVA FUNÇÃO DE TELA
void atualizar_tela(ssd1306_t *, float, float, float, float, float);
void ler_e_exibir_dados(struct bmp280_calib_param *, float*, float*, float*, float*, float*);
void callback_botoes(uint, uint32_t);
void callback_joystick(void);

/* ================================================================ */
int main() {
    stdio_init_all();
    sleep_ms(2000);

    ssd1306_t tela;
    struct bmp280_calib_param parametros_bmp;
    float temperatura_aht = 0, temperatura_bmp = 0, temperatura_media = 0, umidade = 0, pressao = 0;
    uint64_t proxima_leitura = 0;

    configurar_perifericos(&tela, &parametros_bmp);
    configurar_botoes();
    configurar_joystick();

    while (true) {
        if (time_us_64() >= proxima_leitura) {
            ler_e_exibir_dados(&parametros_bmp, &temperatura_aht, &temperatura_bmp,
                               &temperatura_media, &umidade, &pressao);
            proxima_leitura = time_us_64() + INTERVALO_LEITURA_MS * 1000;

            // Armazena os dados nos buffers circulares
            buffer_temperaturas[indice_buffer] = temperatura_media;
            buffer_umidade[indice_buffer] = umidade;
            buffer_pressao[indice_buffer] = pressao / 100.0f; // Armazena em hPa
            
            indice_buffer = (indice_buffer + 1) % TAMANHO_GRAFICO;
            if (contador_buffer < TAMANHO_GRAFICO) contador_buffer++;

            // Atualiza a tela se estiver exibindo dados que mudam com o tempo
            if (tela_atual == TELA_VALORES || tela_atual == TELA_GRAFICO || tela_atual == TELA_UMIDADE || tela_atual == TELA_PRESSAO)
                atualizar_tela_flag = true;
        }
        
        if (atualizar_tela_flag) {
            atualizar_tela(&tela, temperatura_aht, temperatura_bmp, temperatura_media, umidade, pressao);
            atualizar_tela_flag = false;
        }
        
        callback_joystick();
        sleep_ms(10);
    }
    return 0;
}

/* ---------------- Inicialização -------------------------------- */
void configurar_perifericos(ssd1306_t *tela, struct bmp280_calib_param *parametros) {
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

    aht20_init(I2C_SENSORES_PORT);
    bmp280_init(I2C_SENSORES_PORT);
    bmp280_get_calib_params(I2C_SENSORES_PORT, parametros);
}

void configurar_botoes(void) {
    gpio_init(BOTAO_A_PIN);
    gpio_set_dir(BOTAO_A_PIN, GPIO_IN);
    gpio_pull_up(BOTAO_A_PIN);
    gpio_init(BOTAO_B_PIN);
    gpio_set_dir(BOTAO_B_PIN, GPIO_IN);
    gpio_pull_up(BOTAO_B_PIN);
    gpio_set_irq_enabled_with_callback(BOTAO_A_PIN, GPIO_IRQ_EDGE_FALL, true, &callback_botoes);
    gpio_set_irq_enabled_with_callback(BOTAO_B_PIN, GPIO_IRQ_EDGE_FALL, true, &callback_botoes);
}

void configurar_joystick(void) {
    adc_init();
    adc_gpio_init(JOYSTICK_PIN);
    adc_select_input(0);
}

/* ---------------- Callbacks de Interrupção --------------------- */
void callback_botoes(uint gpio, uint32_t eventos) {
    static uint64_t ultimo_pressionamento_a = 0, ultimo_pressionamento_b = 0;
    uint64_t agora = time_us_64();
    if (eventos & GPIO_IRQ_EDGE_FALL) {
        if (gpio == BOTAO_A_PIN && agora - ultimo_pressionamento_a > DEBOUNCE_MS * 1000) {
            tela_atual = (tela_atual + 1) % NUM_TELAS;
            atualizar_tela_flag = true;
            ultimo_pressionamento_a = agora;
        }
        if (gpio == BOTAO_B_PIN && agora - ultimo_pressionamento_b > DEBOUNCE_MS * 1000) {
            tela_atual = (tela_atual == 0) ? (NUM_TELAS - 1) : tela_atual - 1;
            atualizar_tela_flag = true;
            ultimo_pressionamento_b = agora;
        }
    }
}

void callback_joystick(void) {
    if (tela_atual != TELA_GRAFICO && tela_atual != TELA_UMIDADE && tela_atual != TELA_PRESSAO) return;

    const uint16_t ZONA_MORTA_BAIXA = 1500, ZONA_MORTA_ALTA = 2500;
    uint16_t valor = adc_read();
    uint64_t agora = to_ms_since_boot(get_absolute_time());
    if (agora - ultimo_zoom_ms < ZOOM_DEBOUNCE_MS) return;

    bool mudou = false;
    if (tela_atual == TELA_GRAFICO) {
        if (valor > ZONA_MORTA_ALTA && fator_zoom < 4.0f) {
            fator_zoom += 0.10f; mudou = true;
        } else if (valor < ZONA_MORTA_BAIXA && fator_zoom > 0.25f) {
            fator_zoom -= 0.10f; mudou = true;
        }
    } else if (tela_atual == TELA_UMIDADE) {
        if (valor > ZONA_MORTA_ALTA && fator_zoom_umidade < 4.0f) {
            fator_zoom_umidade += 0.10f; mudou = true;
        } else if (valor < ZONA_MORTA_BAIXA && fator_zoom_umidade > 0.25f) {
            fator_zoom_umidade -= 0.10f; mudou = true;
        }
    } else if (tela_atual == TELA_PRESSAO) {
        if (valor > ZONA_MORTA_ALTA && fator_zoom_pressao < 4.0f) {
            fator_zoom_pressao += 0.10f; mudou = true;
        } else if (valor < ZONA_MORTA_BAIXA && fator_zoom_pressao > 0.25f) {
            fator_zoom_pressao -= 0.10f; mudou = true;
        }
    }

    if (mudou) {
        ultimo_zoom_ms = agora;
        atualizar_tela_flag = true;
    }
}

/* ---------------- Funções de Desenho das Telas ----------------- */
void mostrar_tela_abertura(ssd1306_t *tela) {
    ssd1306_fill(tela, 0);
    ssd1306_rect(tela, 5, 5, 118, 54, 1, false);
    const char *texto = "PicoAtmos";
    ssd1306_draw_string(tela, texto, (TELA_LARGURA - strlen(texto) * 8) / 2, 28, false);
    const char *subtexto = "Iniciando..";
    ssd1306_draw_string(tela, subtexto, (TELA_LARGURA - strlen(subtexto) * 6) / 2, 45, true);
    ssd1306_send_data(tela);
}

void mostrar_tela_valores(ssd1306_t *tela, float temp_aht, float temp_bmp, float temp_media, float umid, float press) {
    ssd1306_fill(tela, 0);
    const char *cabecalho = "Valores Medidos";
    ssd1306_draw_string(tela, cabecalho, (TELA_LARGURA - strlen(cabecalho) * 8) / 2, 0, false);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "TempAHT: %.1fC", temp_aht);
    ssd1306_draw_string(tela, buffer, 0, 12, false);
    snprintf(buffer, sizeof(buffer), "TempBMP: %.1fC", temp_bmp);
    ssd1306_draw_string(tela, buffer, 0, 22, false);
    snprintf(buffer, sizeof(buffer), "TempMed: %.1fC", temp_media);
    ssd1306_draw_string(tela, buffer, 0, 32, false);
    snprintf(buffer, sizeof(buffer), "Umidade: %.1f%%", umid);
    ssd1306_draw_string(tela, buffer, 0, 42, false);
    snprintf(buffer, sizeof(buffer), "Pressao: %.1fhPa", press / 100.0);
    ssd1306_draw_string(tela, buffer, 0, 52, false);
    ssd1306_send_data(tela);
}

void mostrar_tela_conexao(ssd1306_t *tela) {
    ssd1306_fill(tela, 0);
    const char *cabecalho = "Conexao";
    ssd1306_draw_string(tela, cabecalho, (TELA_LARGURA - strlen(cabecalho) * 8) / 2, 0, false);
    ssd1306_draw_string(tela, "TempSet:", 0, 10, false);
    ssd1306_draw_string(tela, "UmidSet:", 0, 20, false);
    ssd1306_draw_string(tela, "PresSet:", 0, 30, false);
    ssd1306_draw_string(tela, "IP:", 0, 40, false);
    ssd1306_draw_string(tela, "Status:", 0, 50, false);
    ssd1306_send_data(tela);
}

void mostrar_tela_grafico(ssd1306_t *tela) {
    ssd1306_fill(tela, 0);
    const uint8_t gx = 20, gy = 52, H = 40, W = 105;
    const char *titulo = "Temp media";
    ssd1306_draw_string(tela, titulo, (TELA_LARGURA - strlen(titulo) * 8) / 2, 0, false);

    if (contador_buffer == 0) {
        ssd1306_draw_string(tela, "Aguardando dados...", 8, 30, false);
        ssd1306_send_data(tela);
        return;
    }

    float valor_min = buffer_temperaturas[0], valor_max = valor_min;
    for (int i = 0; i < contador_buffer; ++i) {
        int idx = (indice_buffer - contador_buffer + i + TAMANHO_GRAFICO) % TAMANHO_GRAFICO;
        float valor = buffer_temperaturas[idx];
        if (valor < valor_min) valor_min = valor;
        if (valor > valor_max) valor_max = valor;
    }
    if (valor_max - valor_min < 2.0f) {
        float media = (valor_max + valor_min) / 2.0f;
        valor_min = media - 1.0f;
        valor_max = media + 1.0f;
    }

    float faixa_visivel = (valor_max - valor_min) / fator_zoom;
    float centro = (valor_max + valor_min) * 0.5f;
    float ymin = centro - faixa_visivel * 0.5f;
    float ymax = centro + faixa_visivel * 0.5f;

    float passo_aproximado = faixa_visivel / 3.0f;
    float potencia = powf(10.0f, floorf(log10f(passo_aproximado)));
    float mantissa = passo_aproximado / potencia;
    float passo = (mantissa < 1.5f) ? 1.0f : (mantissa < 3.5f) ? 2.0f : (mantissa < 7.5f) ? 5.0f : 10.0f;
    passo *= potencia;
    if (passo < 0.1f) passo = 0.1f;

    float ymax_marcacao = ceilf(ymax / passo) * passo;
    float ymin_marcacao = floorf(ymin / passo) * passo;
    faixa_visivel = ymax_marcacao - ymin_marcacao;
    if (faixa_visivel < 0.1f) faixa_visivel = 0.1f;

    ssd1306_hline(tela, gx, gx + W, gy, true);
    ssd1306_vline(tela, gx, gy - H, gy, true);

    char buffer[8];
    int num_marcacoes = 0;
    for (float temp = ymax_marcacao; temp >= ymin_marcacao - (passo * 0.1f); temp -= passo) {
        if (num_marcacoes++ >= 4) break;
        uint8_t y = gy - (uint8_t)(((temp - ymin_marcacao) / faixa_visivel) * H);
        if (y > gy || y < (gy - H)) continue;
        ssd1306_hline(tela, gx - 2, gx, y, true);
        if (temp == floorf(temp)) {
            snprintf(buffer, sizeof(buffer), "%.0f", temp);
        } else {
            snprintf(buffer, sizeof(buffer), "%.1f", temp);
        }
        ssd1306_draw_string(tela, buffer, 0, y - 4, false);
    }

    ssd1306_vline(tela, gx, gy, gy + 2, true);
    ssd1306_vline(tela, gx + W / 2, gy, gy + 2, true);
    ssd1306_vline(tela, gx + W, gy, gy + 2, true);
    ssd1306_draw_string(tela, "0", gx - 3, gy + 5, false);
    ssd1306_draw_string(tela, "30s", gx + W / 2 - 10, gy + 5, false);
    ssd1306_draw_string(tela, "60s", gx + W - 18, gy + 5, false);

    if (contador_buffer > 1) {
        for (int i = 0; i < contador_buffer - 1; ++i) {
            int idx_atual = (indice_buffer - contador_buffer + i + TAMANHO_GRAFICO) % TAMANHO_GRAFICO;
            int idx_proximo = (idx_atual + 1) % TAMANHO_GRAFICO;
            float valor1 = buffer_temperaturas[idx_atual];
            float valor2 = buffer_temperaturas[idx_proximo];
            uint8_t x1 = gx + (i * W) / (TAMANHO_GRAFICO - 1);
            uint8_t y1 = gy - (uint8_t)(((valor1 - ymin_marcacao) / faixa_visivel) * H);
            uint8_t x2 = gx + ((i + 1) * W) / (TAMANHO_GRAFICO - 1);
            uint8_t y2 = gy - (uint8_t)(((valor2 - ymin_marcacao) / faixa_visivel) * H);
            ssd1306_line(tela, x1, y1, x2, y2, true);
        }
    }
    ssd1306_send_data(tela);
}

void mostrar_tela_umidade(ssd1306_t *tela) {
    ssd1306_fill(tela, 0);

    const uint8_t gx = 20, gy = 52, H = 40, W = 105;
    const char *titulo = "Umidade";
    ssd1306_draw_string(tela, titulo, (TELA_LARGURA - strlen(titulo) * 8) / 2, 0, false);

    if (contador_buffer == 0) {
        ssd1306_draw_string(tela, "Aguardando dados...", 8, 30, false);
        ssd1306_send_data(tela);
        return;
    }

    float valor_min = buffer_umidade[0], valor_max = valor_min;
    for (int i = 0; i < contador_buffer; ++i) {
        int idx = (indice_buffer - contador_buffer + i + TAMANHO_GRAFICO) % TAMANHO_GRAFICO;
        float valor = buffer_umidade[idx];
        if (valor < valor_min) valor_min = valor;
        if (valor > valor_max) valor_max = valor;
    }
    if (valor_max - valor_min < 10.0f) {
        float media = (valor_max + valor_min) / 2.0f;
        valor_min = media - 5.0f;
        valor_max = media + 5.0f;
    }
    if (valor_min < 0.0f) valor_min = 0.0f;
    if (valor_max > 100.0f) valor_max = 100.0f;

    float faixa_visivel = (valor_max - valor_min) / fator_zoom_umidade;
    float centro = (valor_max + valor_min) * 0.5f;
    float ymin = centro - faixa_visivel * 0.5f;
    float ymax = centro + faixa_visivel * 0.5f;

    float passo_aproximado = faixa_visivel / 3.0f;
    float potencia = powf(10.0f, floorf(log10f(passo_aproximado)));
    float mantissa = passo_aproximado / potencia;
    float passo = (mantissa < 1.5f) ? 1.0f : (mantissa < 3.5f) ? 2.0f : (mantissa < 7.5f) ? 5.0f : 10.0f;
    passo *= potencia;
    if (passo < 1.0f) passo = 1.0f;

    float ymax_marcacao = ceilf(ymax / passo) * passo;
    float ymin_marcacao = floorf(ymin / passo) * passo;
    faixa_visivel = ymax_marcacao - ymin_marcacao;
    if (faixa_visivel < 1.0f) faixa_visivel = 1.0f;

    ssd1306_hline(tela, gx, gx + W, gy, true);
    ssd1306_vline(tela, gx, gy - H, gy, true);

    char buffer[8];
    int num_marcacoes = 0;
    for (float val = ymax_marcacao; val >= ymin_marcacao - (passo * 0.1f); val -= passo) {
        if (num_marcacoes++ >= 4) break;
        uint8_t y = gy - (uint8_t)(((val - ymin_marcacao) / faixa_visivel) * H);
        if (y > gy || y < (gy - H)) continue;
        
        ssd1306_hline(tela, gx - 2, gx, y, true);
        snprintf(buffer, sizeof(buffer), "%.0f", val);
        ssd1306_draw_string(tela, buffer, 0, y - 4, false);
    }
    
    ssd1306_draw_string(tela, "%", 0, gy - H - 8, false);

    ssd1306_vline(tela, gx, gy, gy + 2, true);
    ssd1306_vline(tela, gx + W / 2, gy, gy + 2, true);
    ssd1306_vline(tela, gx + W, gy, gy + 2, true);
    ssd1306_draw_string(tela, "0", gx - 3, gy + 5, false);
    ssd1306_draw_string(tela, "30s", gx + W / 2 - 10, gy + 5, false);
    ssd1306_draw_string(tela, "60s", gx + W - 18, gy + 5, false);

    if (contador_buffer > 1) {
        for (int i = 0; i < contador_buffer - 1; ++i) {
            int idx_atual = (indice_buffer - contador_buffer + i + TAMANHO_GRAFICO) % TAMANHO_GRAFICO;
            int idx_proximo = (idx_atual + 1) % TAMANHO_GRAFICO;
            float valor1 = buffer_umidade[idx_atual];
            float valor2 = buffer_umidade[idx_proximo];
            uint8_t x1 = gx + (i * W) / (TAMANHO_GRAFICO - 1);
            uint8_t y1 = gy - (uint8_t)(((valor1 - ymin_marcacao) / faixa_visivel) * H);
            uint8_t x2 = gx + ((i + 1) * W) / (TAMANHO_GRAFICO - 1);
            uint8_t y2 = gy - (uint8_t)(((valor2 - ymin_marcacao) / faixa_visivel) * H);
            if (y1 >= (gy - H) && y1 <= gy && y2 >= (gy - H) && y2 <= gy) {
                 ssd1306_line(tela, x1, y1, x2, y2, true);
            }
        }
    }
    ssd1306_send_data(tela);
}

/* ---- NOVA TELA: GRÁFICO DE PRESSÃO ---------------------------- */
void mostrar_tela_pressao(ssd1306_t *tela) {
    ssd1306_fill(tela, 0);

    const uint8_t gx = 20, gy = 52, H = 40, W = 105;
    const char *titulo = "Pressao";
    ssd1306_draw_string(tela, titulo, (TELA_LARGURA - strlen(titulo) * 8) / 2, 0, false);

    if (contador_buffer == 0) {
        ssd1306_draw_string(tela, "Aguardando dados...", 8, 30, false);
        ssd1306_send_data(tela);
        return;
    }

    // Encontra min/max no buffer de pressão
    float valor_min = buffer_pressao[0], valor_max = valor_min;
    for (int i = 0; i < contador_buffer; ++i) {
        int idx = (indice_buffer - contador_buffer + i + TAMANHO_GRAFICO) % TAMANHO_GRAFICO;
        float valor = buffer_pressao[idx];
        if (valor < valor_min) valor_min = valor;
        if (valor > valor_max) valor_max = valor;
    }
    // Garante uma faixa vertical mínima de 10 hPa
    if (valor_max - valor_min < 10.0f) {
        float media = (valor_max + valor_min) / 2.0f;
        valor_min = media - 5.0f;
        valor_max = media + 5.0f;
    }

    // Aplica o zoom
    float faixa_visivel = (valor_max - valor_min) / fator_zoom_pressao;
    float centro = (valor_max + valor_min) * 0.5f;
    float ymin = centro - faixa_visivel * 0.5f;
    float ymax = centro + faixa_visivel * 0.5f;

    // Calcula passo "bonito" para as marcações do eixo Y
    float passo_aproximado = faixa_visivel / 3.0f;
    float potencia = powf(10.0f, floorf(log10f(passo_aproximado)));
    float mantissa = passo_aproximado / potencia;
    float passo = (mantissa < 1.5f) ? 1.0f : (mantissa < 3.5f) ? 2.0f : (mantissa < 7.5f) ? 5.0f : 10.0f;
    passo *= potencia;
    if (passo < 1.0f) passo = 1.0f;

    // Ajusta limites para o múltiplo do passo mais próximo
    float ymax_marcacao = ceilf(ymax / passo) * passo;
    float ymin_marcacao = floorf(ymin / passo) * passo;
    faixa_visivel = ymax_marcacao - ymin_marcacao;
    if (faixa_visivel < 1.0f) faixa_visivel = 1.0f;

    // Desenha os eixos
    ssd1306_hline(tela, gx, gx + W, gy, true); // Eixo X
    ssd1306_vline(tela, gx, gy - H, gy, true); // Eixo Y

    // Marcações e rótulos no eixo Y
    char buffer[8];
    int num_marcacoes = 0;
    for (float val = ymax_marcacao; val >= ymin_marcacao - (passo * 0.1f); val -= passo) {
        if (num_marcacoes++ >= 4) break;
        uint8_t y = gy - (uint8_t)(((val - ymin_marcacao) / faixa_visivel) * H);
        if (y > gy || y < (gy - H)) continue;
        
        ssd1306_hline(tela, gx - 2, gx, y, true);
        snprintf(buffer, sizeof(buffer), "%.0f", val); // Mostra como inteiro
        ssd1306_draw_string(tela, buffer, 0, y - 4, false);
    }
    
    // Rótulo da unidade do eixo Y
    ssd1306_draw_string(tela, "hPa", 0, gy - H - 8, false);

    // Marcações e rótulos no eixo X (tempo em segundos)
    ssd1306_vline(tela, gx, gy, gy + 2, true);
    ssd1306_vline(tela, gx + W / 2, gy, gy + 2, true);
    ssd1306_vline(tela, gx + W, gy, gy + 2, true);
    ssd1306_draw_string(tela, "0", gx - 3, gy + 5, false);
    ssd1306_draw_string(tela, "30s", gx + W / 2 - 10, gy + 5, false);
    ssd1306_draw_string(tela, "60s", gx + W - 18, gy + 5, false);

    // Desenha o gráfico de pressão
    if (contador_buffer > 1) {
        for (int i = 0; i < contador_buffer - 1; ++i) {
            int idx_atual = (indice_buffer - contador_buffer + i + TAMANHO_GRAFICO) % TAMANHO_GRAFICO;
            int idx_proximo = (idx_atual + 1) % TAMANHO_GRAFICO;

            float valor1 = buffer_pressao[idx_atual];
            float valor2 = buffer_pressao[idx_proximo];

            uint8_t x1 = gx + (i * W) / (TAMANHO_GRAFICO - 1);
            uint8_t y1 = gy - (uint8_t)(((valor1 - ymin_marcacao) / faixa_visivel) * H);
            uint8_t x2 = gx + ((i + 1) * W) / (TAMANHO_GRAFICO - 1);
            uint8_t y2 = gy - (uint8_t)(((valor2 - ymin_marcacao) / faixa_visivel) * H);

            if (y1 >= (gy - H) && y1 <= gy && y2 >= (gy - H) && y2 <= gy) {
                 ssd1306_line(tela, x1, y1, x2, y2, true);
            }
        }
    }

    ssd1306_send_data(tela);
}


/* ---------------- Funções de Controle -------------------------- */
void atualizar_tela(ssd1306_t *tela, float temp_aht, float temp_bmp, float temp_media, float umid, float press) {
    switch (tela_atual) {
        case TELA_ABERTURA: mostrar_tela_abertura(tela); break;
        case TELA_VALORES:  mostrar_tela_valores(tela, temp_aht, temp_bmp, temp_media, umid, press); break;
        case TELA_CONEXAO:  mostrar_tela_conexao(tela); break;
        case TELA_GRAFICO:  mostrar_tela_grafico(tela); break;
        case TELA_UMIDADE:  mostrar_tela_umidade(tela); break;
        case TELA_PRESSAO:  mostrar_tela_pressao(tela); break; // ATUALIZADO
    }
}

void ler_e_exibir_dados(struct bmp280_calib_param *parametros, float *temp_aht, float *temp_bmp,
                        float *temp_media, float *umid, float *press) {
    AHT20_Data dados;
    aht20_read(I2C_SENSORES_PORT, &dados);
    *temp_aht = dados.temperature;
    *umid = dados.humidity;
    int32_t temp_raw, press_raw;
    bmp280_read_raw(I2C_SENSORES_PORT, &temp_raw, &press_raw);
    int32_t temp_convertida = bmp280_convert_temp(temp_raw, parametros);
    int32_t press_convertida = bmp280_convert_pressure(press_raw, temp_raw, parametros);
    *temp_bmp = temp_convertida / 100.0f;
    *press = press_convertida;
    *temp_media = (*temp_aht + *temp_bmp) / 2.0f;
}
