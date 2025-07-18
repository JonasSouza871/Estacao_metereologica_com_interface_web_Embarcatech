#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwipopts.h"
#include "lwipopts_examples_common.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/irq.h"
#include "lwip/tcp.h"
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

/* ---- Configurações de Wi-Fi ----------------------------------- */
#define WIFI_SSID "Jonas Souza"
#define WIFI_PASS "12345678"

/* ---- Configurações de Estado e Tempo -------------------------- */
#define TELA_ABERTURA           0
#define TELA_VALORES            1
#define TELA_CONEXAO            2
#define TELA_GRAFICO            3
#define TELA_UMIDADE            4
#define TELA_PRESSAO            5
#define NUM_TELAS               6
#define DEBOUNCE_MS             250
#define INTERVALO_LEITURA_MS    2000
#define TAMANHO_GRAFICO         30

/* ---- Configurações de Setpoints ------------------------------- */
float temperatura_setpoint = 25.0f;
float umidade_setpoint = 60.0f;
float pressao_setpoint = 1013.25f;

/* ---- Status de Conexão ---------------------------------------- */
bool wifi_conectado = false;
char ip_endereco[24] = "Desconectado";

/* ---- Variáveis Globais ---------------------------------------- */
volatile uint8_t tela_atual = TELA_ABERTURA;
volatile bool    atualizar_tela_flag = true;
volatile float   fator_zoom = 1.0f;
volatile float   fator_zoom_umidade = 1.0f;
volatile float   fator_zoom_pressao = 1.0f;

static uint64_t  ultimo_zoom_ms = 0;
const  uint32_t  ZOOM_DEBOUNCE_MS = 120;

// Buffers para os gráficos
float buffer_temperaturas[TAMANHO_GRAFICO];
float buffer_umidade[TAMANHO_GRAFICO];
float buffer_pressao[TAMANHO_GRAFICO];
int   indice_buffer = 0;
int   contador_buffer = 0;

// Variáveis para leituras dos sensores
float temperatura_aht = 0, temperatura_bmp = 0, temperatura_media = 0;
float umidade = 0, pressao = 0;

/* ---- Estrutura HTTP ------------------------------------------- */
struct http_state {
    char response[8192];
    size_t len;
    size_t sent;
};

/* ---- Página HTML ---------------------------------------------- */
const char HTML_BODY[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>PicoAtmos - Monitor Atmosférico</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<style>"
    "body { font-family: sans-serif; text-align: center; padding: 20px; margin: 0; background: #f0f8ff; }"
    ".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }"
    "h1 { color: #2c3e50; margin-top: 0; }"
    ".sensor-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 15px; margin: 20px 0; }"
    ".sensor-card { background: #f8f9fa; padding: 15px; border-radius: 8px; border-left: 4px solid #3498db; }"
    ".sensor-value { font-size: 24px; font-weight: bold; color: #2c3e50; }"
    ".sensor-unit { font-size: 14px; color: #7f8c8d; }"
    ".setpoint-container { margin: 20px 0; }"
    ".setpoint-row { display: flex; justify-content: space-between; align-items: center; margin: 10px 0; }"
    "input { padding: 8px; border: 1px solid #ddd; border-radius: 4px; width: 80px; }"
    "button { background: #2ecc71; color: white; border: none; padding: 8px 15px; border-radius: 4px; cursor: pointer; }"
    "button:hover { background: #27ae60; }"
    ".status-info { background: #ecf0f1; padding: 10px; border-radius: 4px; margin-top: 15px; }"
    "</style>"
    "<script>"
    "function atualizarSetpoints() {"
    "  const temp = document.getElementById('temp_setpoint').value;"
    "  const umid = document.getElementById('umid_setpoint').value;"
    "  const press = document.getElementById('press_setpoint').value;"
    "  fetch('/set_setpoints?temp=' + temp + '&umid=' + umid + '&press=' + press)"
    "    .then(response => response.text())"
    "    .then(data => console.log(data));"
    "}"
    "function atualizarDados() {"
    "  fetch('/dados').then(res => res.json()).then(data => {"
    "    document.getElementById('temp_atual').innerText = data.temp_media.toFixed(1);"
    "    document.getElementById('umid_atual').innerText = data.umidade.toFixed(1);"
    "    document.getElementById('press_atual').innerText = data.pressao.toFixed(1);"
    "    document.getElementById('temp_set_display').innerText = data.temp_setpoint.toFixed(1);"
    "    document.getElementById('umid_set_display').innerText = data.umid_setpoint.toFixed(1);"
    "    document.getElementById('press_set_display').innerText = data.press_setpoint.toFixed(1);"
    "  });"
    "}"
    "setInterval(atualizarDados, 2000);"
    "</script></head><body>"
    "<div class='container'>"
    "<h1>🌡️ PicoAtmos - Monitor Atmosférico</h1>"
    
    "<div class='sensor-grid'>"
    "<div class='sensor-card'>"
    "<div>Temperatura</div>"
    "<div class='sensor-value'><span id='temp_atual'>--</span><span class='sensor-unit'>°C</span></div>"
    "</div>"
    "<div class='sensor-card'>"
    "<div>Umidade</div>"
    "<div class='sensor-value'><span id='umid_atual'>--</span><span class='sensor-unit'>%</span></div>"
    "</div>"
    "<div class='sensor-card'>"
    "<div>Pressão</div>"
    "<div class='sensor-value'><span id='press_atual'>--</span><span class='sensor-unit'>hPa</span></div>"
    "</div>"
    "</div>"
    
    "<div class='setpoint-container'>"
    "<h3>Configurações de Setpoint</h3>"
    "<div class='setpoint-row'>"
    "<span>Temperatura: <span id='temp_set_display'>--</span>°C</span>"
    "<input type='number' id='temp_setpoint' step='0.1' value='25.0'>"
    "</div>"
    "<div class='setpoint-row'>"
    "<span>Umidade: <span id='umid_set_display'>--</span>%</span>"
    "<input type='number' id='umid_setpoint' step='1' value='60'>"
    "</div>"
    "<div class='setpoint-row'>"
    "<span>Pressão: <span id='press_set_display'>--</span>hPa</span>"
    "<input type='number' id='press_setpoint' step='0.1' value='1013.25'>"
    "</div>"
    "<button onclick='atualizarSetpoints()'>Salvar Configurações</button>"
    "</div>"
    
    "<div class='status-info'>"
    "<div>Sistema PicoAtmos ativo</div>"
    "<div>Atualizando a cada 2 segundos</div>"
    "</div>"
    "</div>"
    "</body></html>";

/* ---- Protótipos de Funções ------------------------------------ */
void configurar_perifericos(ssd1306_t *, struct bmp280_calib_param *);
void configurar_botoes(void);
void configurar_joystick(void);
void configurar_wifi(ssd1306_t *);
static void start_http_server(void);
void mostrar_tela_abertura(ssd1306_t *);
void mostrar_tela_valores(ssd1306_t *, float, float, float, float, float);
void mostrar_tela_conexao(ssd1306_t *);
void mostrar_tela_grafico(ssd1306_t *);
void mostrar_tela_umidade(ssd1306_t *);
void mostrar_tela_pressao(ssd1306_t *);
void atualizar_tela(ssd1306_t *, float, float, float, float, float);
void ler_e_exibir_dados(struct bmp280_calib_param *, float*, float*, float*, float*, float*);
void callback_botoes(uint, uint32_t);
void callback_joystick(void);

/* ---------------- Funções de Servidor HTTP --------------------- */
static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len) {
        tcp_close(tpcb);
        free(hs);
    }
    return ERR_OK;
}

static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *req = (char *)p->payload;
    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs) {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;

    // Responde com dados dos sensores em JSON
    if (strstr(req, "GET /dados")) {
        char json_payload[256];
        int json_len = snprintf(json_payload, sizeof(json_payload),
                                "{\"temp_media\":%.2f,\"umidade\":%.2f,\"pressao\":%.2f,"
                                "\"temp_setpoint\":%.2f,\"umid_setpoint\":%.2f,\"press_setpoint\":%.2f}",
                                temperatura_media, umidade, pressao/100.0f,
                                temperatura_setpoint, umidade_setpoint, pressao_setpoint);

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           json_len, json_payload);
    }
    // Atualiza setpoints via requisição HTTP
    else if (strstr(req, "GET /set_setpoints")) {
        float temp, umid, press;
        sscanf(req, "GET /set_setpoints?temp=%f&umid=%f&press=%f", &temp, &umid, &press);
        temperatura_setpoint = temp;
        umidade_setpoint = umid;
        pressao_setpoint = press;

        const char *txt = "Setpoints atualizados";
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(txt), txt);
    }
    // Envia página HTML padrão
    else {
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(HTML_BODY), HTML_BODY);
    }

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);
    tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    pbuf_free(p);
    return ERR_OK;
}

static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

static void start_http_server(void) {
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        printf("Erro ao criar PCB TCP\n");
        return;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) {
        printf("Erro ao ligar o servidor na porta 80\n");
        return;
    }
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
    printf("Servidor HTTP rodando na porta 80...\n");
}

/* ================================================================ */
int main() {
    stdio_init_all();
    sleep_ms(2000);

    ssd1306_t tela;
    struct bmp280_calib_param parametros_bmp;
    uint64_t proxima_leitura = 0;

    configurar_perifericos(&tela, &parametros_bmp);
    configurar_botoes();
    configurar_joystick();
    configurar_wifi(&tela);

    while (true) {
        // Atualiza estado da rede Wi-Fi
        if (wifi_conectado) {
            cyw43_arch_poll();
        }

        if (time_us_64() >= proxima_leitura) {
            ler_e_exibir_dados(&parametros_bmp, &temperatura_aht, &temperatura_bmp,
                               &temperatura_media, &umidade, &pressao);
            proxima_leitura = time_us_64() + INTERVALO_LEITURA_MS * 1000;

            // Armazena os dados nos buffers circulares
            buffer_temperaturas[indice_buffer] = temperatura_media;
            buffer_umidade[indice_buffer] = umidade;
            buffer_pressao[indice_buffer] = pressao / 100.0f;
            
            indice_buffer = (indice_buffer + 1) % TAMANHO_GRAFICO;
            if (contador_buffer < TAMANHO_GRAFICO) contador_buffer++;

            // Atualiza a tela se estiver exibindo dados que mudam com o tempo
            if (tela_atual == TELA_VALORES || tela_atual == TELA_GRAFICO || 
                tela_atual == TELA_UMIDADE || tela_atual == TELA_PRESSAO)
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

void configurar_wifi(ssd1306_t *tela) {
    ssd1306_fill(tela, 0);
    ssd1306_draw_string(tela, "Iniciando Wi-Fi", 0, 0, false);
    ssd1306_draw_string(tela, "Aguarde...", 0, 30, false);
    ssd1306_send_data(tela);

    if (cyw43_arch_init()) {
        ssd1306_fill(tela, 0);
        ssd1306_draw_string(tela, "WiFi => FALHA", 0, 0, false);
        ssd1306_send_data(tela);
        wifi_conectado = false;
        strcpy(ip_endereco, "Erro Init");
        return;
    }

    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
        ssd1306_fill(tela, 0);
        ssd1306_draw_string(tela, "WiFi => ERRO", 0, 0, false);
        ssd1306_send_data(tela);
        wifi_conectado = false;
        strcpy(ip_endereco, "Erro Conexao");
        return;
    }

    // Obtém o endereço IP
    uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
    snprintf(ip_endereco, sizeof(ip_endereco), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    
    ssd1306_fill(tela, 0);
    ssd1306_draw_string(tela, "WiFi => OK", 0, 0, false);
    ssd1306_draw_string(tela, ip_endereco, 0, 10, false);
    ssd1306_send_data(tela);
    
    wifi_conectado = true;
    start_http_server();
    sleep_ms(2000);
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
    const char *cabecalho = "Status da Rede";
    ssd1306_draw_string(tela, cabecalho, (TELA_LARGURA - strlen(cabecalho) * 8) / 2, 0, false);
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "IP: %s", ip_endereco);
    ssd1306_draw_string(tela, buffer, 0, 25, false);
    snprintf(buffer, sizeof(buffer), "WiFi: %s", wifi_conectado ? "CONECTADO" : "DESCONECTADO");
    ssd1306_draw_string(tela, buffer, 0, 40, false);
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

    float valor_min = buffer_pressao[0], valor_max = valor_min;
    for (int i = 0; i < contador_buffer; ++i) {
        int idx = (indice_buffer - contador_buffer + i + TAMANHO_GRAFICO) % TAMANHO_GRAFICO;
        float valor = buffer_pressao[idx];
        if (valor < valor_min) valor_min = valor;
        if (valor > valor_max) valor_max = valor;
    }
    if (valor_max - valor_min < 10.0f) {
        float media = (valor_max + valor_min) / 2.0f;
        valor_min = media - 5.0f;
        valor_max = media + 5.0f;
    }

    float faixa_visivel = (valor_max - valor_min) / fator_zoom_pressao;
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
    
    ssd1306_draw_string(tela, "hPa", 0, gy - H - 8, false);

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
        case TELA_PRESSAO:  mostrar_tela_pressao(tela); break;
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
