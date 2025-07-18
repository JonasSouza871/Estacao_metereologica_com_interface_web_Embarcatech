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

// Inclusões do projeto
#include "aht20.h"
#include "bmp280.h"
#include "ssd1306.h"
#include "font.h"
#include "matriz_led.h" // <-- INCLUSÃO DA BIBLIOTECA DA MATRIZ

/* ---- Configurações de Hardware -------------------------------- */
#define I2C_SENSORES_PORT i2c0
#define I2C_SENSORES_SDA_PIN 0
#define I2C_SENSORES_SCL_PIN 1
#define I2C_TELA_PORT i2c1
#define I2C_TELA_SDA_PIN 14
#define I2C_TELA_SCL_PIN 15
#define TELA_ENDERECO 0x3C
#define TELA_LARGURA 128
#define TELA_ALTURA 64
#define BOTAO_A_PIN 5
#define BOTAO_B_PIN 6
#define JOYSTICK_PIN 26

/* ---- Configurações de LEDs RGB -------------------------------- */
#define LED_VERDE_PIN 11
#define LED_AZUL_PIN 12
#define LED_VERMELHO_PIN 13

/* ---- Configurações de Wi-Fi ----------------------------------- */
#define WIFI_SSID "Jonas Souza"
#define WIFI_PASS "12345678"

/* ---- Configurações de Estado e Tempo -------------------------- */
#define TELA_ABERTURA 0
#define TELA_VALORES 1
#define TELA_CONEXAO 2
#define TELA_GRAFICO 3
#define TELA_UMIDADE 4
#define TELA_PRESSAO 5
#define TELA_LED_STATUS 6
#define NUM_TELAS 7
#define DEBOUNCE_MS 250
#define INTERVALO_LEITURA_MS 2000
#define TAMANHO_GRAFICO 30
#define TAMANHO_HISTORICO_WEB 100

/* ---- Configurações de Limites --------------------------------- */
float temperatura_limite_inferior = 20.0f;
float temperatura_limite_superior = 30.0f;
float umidade_limite_inferior = 40.0f;
float umidade_limite_superior = 80.0f;
float pressao_limite_inferior = 1000.0f;
float pressao_limite_superior = 1030.0f;

/* ---- Status de Conexão ---------------------------------------- */
bool wifi_conectado = false;
char ip_endereco[24] = "Desconectado";

/* ---- Variáveis de Controle ------------------------------------ */
volatile uint8_t tela_atual = TELA_ABERTURA;
volatile bool atualizar_tela_flag = true;
volatile float fator_zoom = 1.0f;
volatile float fator_zoom_umidade = 1.0f;
volatile float fator_zoom_pressao = 1.0f;
static uint64_t ultimo_zoom_ms = 0;
const uint32_t ZOOM_DEBOUNCE_MS = 120;

// Variável global para armazenar o estado atual do sistema
volatile EstadoSistema estado_global = ESTADO_NORMAL;

// Buffers para os gráficos do display
float buffer_temperaturas[TAMANHO_GRAFICO];
float buffer_umidade[TAMANHO_GRAFICO];
float buffer_pressao[TAMANHO_GRAFICO];
int indice_buffer = 0;
int contador_buffer = 0;

// Buffers para os gráficos web (histórico maior)
float historico_temp_media[TAMANHO_HISTORICO_WEB];
float historico_umidade[TAMANHO_HISTORICO_WEB];
float historico_pressao[TAMANHO_HISTORICO_WEB];
int indice_historico = 0;
int contador_historico = 0;

// Variáveis para leituras dos sensores
float temperatura_aht = 0, temperatura_bmp = 0, temperatura_media = 0;
float umidade = 0, pressao = 0;

/* ---- Estrutura HTTP ------------------------------------------- */
struct http_state {
    char response[12288];
    size_t len;
    size_t sent;
};

/* ---- Página HTML (sem alterações) ----------------------------- */
const char HTML_BODY[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>PicoAtmos - Monitor Atmosférico</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"
    "<style>"
    "body { font-family: sans-serif; text-align: center; padding: 20px; margin: 0; background: #f0f8ff; }"
    ".container { max-width: 1200px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }"
    "h1 { color: #2c3e50; margin-top: 0; }"
    ".sensor-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 15px; margin: 20px 0; }"
    ".sensor-card { background: #f8f9fa; padding: 15px; border-radius: 8px; border-left: 4px solid #3498db; }"
    ".sensor-value { font-size: 24px; font-weight: bold; color: #2c3e50; }"
    ".sensor-unit { font-size: 14px; color: #7f8c8d; }"
    ".limits-container { margin: 20px 0; }"
    ".limits-section { margin: 15px 0; padding: 15px; background: #f8f9fa; border-radius: 8px; }"
    ".limits-title { font-weight: bold; margin-bottom: 10px; color: #2c3e50; }"
    ".limits-row { display: flex; justify-content: space-between; align-items: center; margin: 8px 0; flex-wrap: wrap; gap: 10px; }"
    ".limits-inputs { display: flex; gap: 10px; align-items: center; }"
    "input { padding: 8px; border: 1px solid #ddd; border-radius: 4px; width: 80px; }"
    "button { background: #2ecc71; color: white; border: none; padding: 8px 15px; border-radius: 4px; cursor: pointer; }"
    "button:hover { background: #27ae60; }"
    ".status-info { background: #ecf0f1; padding: 10px; border-radius: 4px; margin-top: 15px; }"
    ".range-display { font-size: 14px; color: #7f8c8d; }"
    ".charts-container { margin: 30px 0; }"
    ".charts-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(400px, 1fr)); gap: 20px; }"
    ".chart-card { background: #f8f9fa; padding: 20px; border-radius: 8px; border: 1px solid #ddd; }"
    ".chart-title { font-weight: bold; margin-bottom: 15px; color: #2c3e50; text-align: center; }"
    "canvas { max-width: 100%; height: 300px !important; }"
    ".system-status { margin: 30px 0; padding: 20px; background: #f8f9fa; border-radius: 8px; border: 1px solid #ddd; }"
    ".system-status h3 { color: #2c3e50; margin-top: 0; }"
    ".status-list { display: flex; flex-direction: column; gap: 10px; margin: 20px 0; }"
    ".status-item { padding: 10px; border-radius: 5px; text-align: left; }"
    ".status-item.baixo { background: #cce7ff; color: #004085; }"
    ".status-item.normal { background: #d4edda; color: #155724; }"
    ".status-item.alto { background: #f8d7da; color: #721c24; }"
    ".status-label { font-size: 16px; font-weight: bold; }"
    ".status-value { font-size: 16px; }"
    "</style>"
    "<script>"
    "let charts = {};"
    "let chartData = {"
    "tempMedia: [], umidade: [], pressao: [],"
    "labels: []"
    "};"
    "function atualizarLimites() {"
    "const tempMin = document.getElementById('temp_min').value;"
    "const tempMax = document.getElementById('temp_max').value;"
    "const umidMin = document.getElementById('umid_min').value;"
    "const umidMax = document.getElementById('umid_max').value;"
    "const pressMin = document.getElementById('press_min').value;"
    "const pressMax = document.getElementById('press_max').value;"
    "fetch('/set_limits?temp_min=' + tempMin + '&temp_max=' + tempMax + '&umid_min=' + umidMin + '&umid_max=' + umidMax + '&press_min=' + pressMin + '&press_max=' + pressMax)"
    ".then(response => response.text())"
    ".then(data => console.log(data));"
    "}"
    "function atualizarStatusSistema(data) {"
    "const tempStatus = document.getElementById('temp_status');"
    "const tempValue = tempStatus.querySelector('.status-value');"
    "if (data.temp_media < data.temp_min) {"
    "tempStatus.className = 'status-item baixo';"
    "tempValue.textContent = 'Baixa';"
    "} else if (data.temp_media > data.temp_max) {"
    "tempStatus.className = 'status-item alto';"
    "tempValue.textContent = 'Alta';"
    "} else {"
    "tempStatus.className = 'status-item normal';"
    "tempValue.textContent = 'Normal';"
    "}"
    "const umidStatus = document.getElementById('umid_status');"
    "const umidValue = umidStatus.querySelector('.status-value');"
    "if (data.umidade < data.umid_min) {"
    "umidStatus.className = 'status-item baixo';"
    "umidValue.textContent = 'Baixa';"
    "} else if (data.umidade > data.umid_max) {"
    "umidStatus.className = 'status-item alto';"
    "umidValue.textContent = 'Alta';"
    "} else {"
    "umidStatus.className = 'status-item normal';"
    "umidValue.textContent = 'Normal';"
    "}"
    "const pressStatus = document.getElementById('press_status');"
    "const pressValue = pressStatus.querySelector('.status-value');"
    "if (data.pressao < data.press_min) {"
    "pressStatus.className = 'status-item baixo';"
    "pressValue.textContent = 'Baixa';"
    "} else if (data.pressao > data.press_max) {"
    "pressStatus.className = 'status-item alto';"
    "pressValue.textContent = 'Alta';"
    "} else {"
    "pressStatus.className = 'status-item normal';"
    "pressValue.textContent = 'Normal';"
    "}"
    "}"
    "function criarGraficos() {"
    "const ctx1 = document.getElementById('chartTempMedia').getContext('2d');"
    "const ctx2 = document.getElementById('chartUmidade').getContext('2d');"
    "const ctx3 = document.getElementById('chartPressao').getContext('2d');"
    "charts.tempMedia = new Chart(ctx1, {"
    "type: 'line',"
    "data: {"
    "labels: chartData.labels,"
    "datasets: [{"
    "label: 'Temperatura Média (°C)',"
    "data: chartData.tempMedia,"
    "borderColor: '#e74c3c',"
    "backgroundColor: 'rgba(231, 76, 60, 0.1)',"
    "tension: 0.4"
    "}]"
    "},"
    "options: {"
    "responsive: true,"
    "maintainAspectRatio: false,"
    "scales: {"
    "y: { beginAtZero: false }"
    "}"
    "}"
    "});"
    "charts.umidade = new Chart(ctx2, {"
    "type: 'line',"
    "data: {"
    "labels: chartData.labels,"
    "datasets: [{"
    "label: 'Umidade (%)',"
    "data: chartData.umidade,"
    "borderColor: '#3498db',"
    "backgroundColor: 'rgba(52, 152, 219, 0.1)',"
    "tension: 0.4"
    "}]"
    "},"
    "options: {"
    "responsive: true,"
    "maintainAspectRatio: false,"
    "scales: {"
    "y: { beginAtZero: true, max: 100 }"
    "}"
    "}"
    "});"
    "charts.pressao = new Chart(ctx3, {"
    "type: 'line',"
    "data: {"
    "labels: chartData.labels,"
    "datasets: [{"
    "label: 'Pressão (hPa)',"
    "data: chartData.pressao,"
    "borderColor: '#27ae60',"
    "backgroundColor: 'rgba(39, 174, 96, 0.1)',"
    "tension: 0.4"
    "}]"
    "},"
    "options: {"
    "responsive: true,"
    "maintainAspectRatio: false,"
    "scales: {"
    "y: { beginAtZero: false }"
    "}"
    "}"
    "});"
    "}"
    "function atualizarGraficos(data) {"
    "const agora = new Date();"
    "const tempo = agora.toLocaleTimeString();"
    "chartData.tempMedia.push(data.temp_media);"
    "chartData.umidade.push(data.umidade);"
    "chartData.pressao.push(data.pressao);"
    "chartData.labels.push(tempo);"
    "const maxPoints = 30;"
    "if (chartData.labels.length > maxPoints) {"
    "chartData.tempMedia.shift();"
    "chartData.umidade.shift();"
    "chartData.pressao.shift();"
    "chartData.labels.shift();"
    "}"
    "Object.values(charts).forEach(chart => chart.update('none'));"
    "}"
    "function atualizarDados() {"
    "fetch('/dados').then(res => res.json()).then(data => {"
    "document.getElementById('temp_aht').innerText = data.temp_aht.toFixed(1);"
    "document.getElementById('temp_bmp').innerText = data.temp_bmp.toFixed(1);"
    "document.getElementById('temp_atual').innerText = data.temp_media.toFixed(1);"
    "document.getElementById('umid_atual').innerText = data.umidade.toFixed(1);"
    "document.getElementById('press_atual').innerText = data.pressao.toFixed(1);"
    "document.getElementById('temp_range_display').innerText = data.temp_min.toFixed(1) + ' - ' + data.temp_max.toFixed(1);"
    "document.getElementById('umid_range_display').innerText = data.umid_min.toFixed(1) + ' - ' + data.umid_max.toFixed(1);"
    "document.getElementById('press_range_display').innerText = data.press_min.toFixed(1) + ' - ' + data.press_max.toFixed(1);"
    "atualizarGraficos(data);"
    "atualizarStatusSistema(data);"
    "});"
    "}"
    "window.onload = function() {"
    "criarGraficos();"
    "atualizarDados();"
    "setInterval(atualizarDados, 2000);"
    "};"
    "</script></head><body>"
    "<div class='container'>"
    "<h1>🌡️ PicoAtmos - Monitor Atmosférico</h1>"
    "<div class='sensor-grid'>"
    "<div class='sensor-card'>"
    "<div>Temperatura AHT</div>"
    "<div class='sensor-value'><span id='temp_aht'>--</span><span class='sensor-unit'>°C</span></div>"
    "</div>"
    "<div class='sensor-card'>"
    "<div>Temperatura BMP</div>"
    "<div class='sensor-value'><span id='temp_bmp'>--</span><span class='sensor-unit'>°C</span></div>"
    "</div>"
    "<div class='sensor-card'>"
    "<div>Temperatura Média</div>"
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
    "<div class='limits-container'>"
    "<h3>Configurações de Limites</h3>"
    "<div class='limits-section'>"
    "<div class='limits-title'>Temperatura</div>"
    "<div class='limits-row'>"
    "<span>Faixa: <span id='temp_range_display' class='range-display'>--</span>°C</span>"
    "<div class='limits-inputs'>"
    "<input type='number' id='temp_min' step='0.1' value='20.0' placeholder='Mín'>"
    "<span>-</span>"
    "<input type='number' id='temp_max' step='0.1' value='30.0' placeholder='Máx'>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='limits-section'>"
    "<div class='limits-title'>Umidade</div>"
    "<div class='limits-row'>"
    "<span>Faixa: <span id='umid_range_display' class='range-display'>--</span>%</span>"
    "<div class='limits-inputs'>"
    "<input type='number' id='umid_min' step='1' value='40' placeholder='Mín'>"
    "<span>-</span>"
    "<input type='number' id='umid_max' step='1' value='80' placeholder='Máx'>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='limits-section'>"
    "<div class='limits-title'>Pressão</div>"
    "<div class='limits-row'>"
    "<span>Faixa: <span id='press_range_display' class='range-display'>--</span>hPa</span>"
    "<div class='limits-inputs'>"
    "<input type='number' id='press_min' step='0.1' value='1000.0' placeholder='Mín'>"
    "<span>-</span>"
    "<input type='number' id='press_max' step='0.1' value='1030.0' placeholder='Máx'>"
    "</div>"
    "</div>"
    "</div>"
    "<button onclick='atualizarLimites()'>Salvar Limites</button>"
    "</div>"
    "<div class='charts-container'>"
    "<h3>Gráficos em Tempo Real</h3>"
    "<div class='charts-grid'>"
    "<div class='chart-card'>"
    "<div class='chart-title'>Temperatura Média</div>"
    "<canvas id='chartTempMedia'></canvas>"
    "</div>"
    "<div class='chart-card'>"
    "<div class='chart-title'>Umidade</div>"
    "<canvas id='chartUmidade'></canvas>"
    "</div>"
    "<div class='chart-card'>"
    "<div class='chart-title'>Pressão</div>"
    "<canvas id='chartPressao'></canvas>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='system-status'>"
    "<h3>📊 Estado do Sistema</h3>"
    "<div class='status-list'>"
    "<div id='temp_status' class='status-item'>"
    "<span class='status-label'>Temperatura: </span><span class='status-value'>--</span>"
    "</div>"
    "<div id='umid_status' class='status-item'>"
    "<span class='status-label'>Umidade: </span><span class='status-value'>--</span>"
    "</div>"
    "<div id='press_status' class='status-item'>"
    "<span class='status-label'>Pressão: </span><span class='status-value'>--</span>"
    "</div>"
    "</div>"
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
void configurar_leds_rgb(void);
void configurar_wifi(ssd1306_t *);
static void start_http_server(void);
void mostrar_tela_abertura(ssd1306_t *);
void mostrar_tela_valores(ssd1306_t *, float, float, float, float, float);
void mostrar_tela_conexao(ssd1306_t *);
void mostrar_tela_grafico(ssd1306_t *);
void mostrar_tela_umidade(ssd1306_t *);
void mostrar_tela_pressao(ssd1306_t *);
void mostrar_tela_led_status(ssd1306_t *);
void atualizar_tela(ssd1306_t *, float, float, float, float, float);
void ler_e_exibir_dados(struct bmp280_calib_param *, float *, float *, float *, float *, float *);
void callback_botoes(uint, uint32_t);
void callback_joystick(void);

// Funções novas e modificadas
EstadoSistema obter_estado_sistema(void);
void atualizar_leds_rgb(EstadoSistema estado);
const char* estado_para_string_cor(EstadoSistema estado);
const char* estado_para_string_animacao(EstadoSistema estado);


/* ---------------- Funções de Servidor HTTP (sem alterações) ---- */
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
    if (strstr(req, "GET /dados")) {
        char json_payload[512];
        int json_len = snprintf(json_payload, sizeof(json_payload),
            "{\"temp_aht\":%.2f,\"temp_bmp\":%.2f,\"temp_media\":%.2f,\"umidade\":%.2f,\"pressao\":%.2f,"
            "\"temp_min\":%.2f,\"temp_max\":%.2f,\"umid_min\":%.2f,\"umid_max\":%.2f,"
            "\"press_min\":%.2f,\"press_max\":%.2f}",
            temperatura_aht, temperatura_bmp, temperatura_media, umidade, pressao / 100.0f,
            temperatura_limite_inferior, temperatura_limite_superior,
            umidade_limite_inferior, umidade_limite_superior,
            pressao_limite_inferior, pressao_limite_superior);
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            json_len, json_payload);
    }
    else if (strstr(req, "GET /set_limits")) {
        float temp_min, temp_max, umid_min, umid_max, press_min, press_max;
        sscanf(req, "GET /set_limits?temp_min=%f&temp_max=%f&umid_min=%f&umid_max=%f&press_min=%f&press_max=%f",
            &temp_min, &temp_max, &umid_min, &umid_max, &press_min, &press_max);

        temperatura_limite_inferior = temp_min;
        temperatura_limite_superior = temp_max;
        umidade_limite_inferior = umid_min;
        umidade_limite_superior = umid_max;
        pressao_limite_inferior = press_min;
        pressao_limite_superior = press_max;
        const char *txt = "Limites atualizados";
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            (int)strlen(txt), txt);
    }
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

/* ---------------- Funções de Lógica de Estado ----------------- */

/**
 * @brief Determina o estado atual do sistema com base nas leituras dos sensores.
 * A prioridade é: Temperatura > Umidade > Pressão.
 * @return O estado atual do sistema (enum EstadoSistema).
 */
EstadoSistema obter_estado_sistema(void) {
    float pressao_hpa = pressao / 100.0f;
    
    // Prioridade 1: Temperatura
    if (temperatura_media > temperatura_limite_superior) return ESTADO_TEMP_ALTA;
    if (temperatura_media < temperatura_limite_inferior) return ESTADO_TEMP_BAIXA;
    
    // Prioridade 2: Umidade
    if (umidade > umidade_limite_superior) return ESTADO_UMID_ALTA;
    if (umidade < umidade_limite_inferior) return ESTADO_UMID_BAIXA;
    
    // Prioridade 3: Pressão
    if (pressao_hpa > pressao_limite_superior) return ESTADO_PRESS_ALTA;
    if (pressao_hpa < pressao_limite_inferior) return ESTADO_PRESS_BAIXA;
    
    // Se nenhum limite foi ultrapassado
    return ESTADO_NORMAL;
}

/**
 * @brief Converte o enum EstadoSistema para uma string de cor para exibição.
 */
const char* estado_para_string_cor(EstadoSistema estado) {
    switch (estado) {
        case ESTADO_NORMAL:       return "Verde";
        case ESTADO_TEMP_ALTA:    return "Vermelho";
        case ESTADO_TEMP_BAIXA:   return "Azul";
        case ESTADO_UMID_ALTA:    return "Roxo";
        case ESTADO_UMID_BAIXA:   return "Amarelo";
        case ESTADO_PRESS_ALTA:   return "Branco";
        case ESTADO_PRESS_BAIXA:  return "Cinza (Matriz)";
        default:                  return "Desconhecido";
    }
}

/**
 * @brief Converte o enum EstadoSistema para uma string de animação para exibição.
 */
const char* estado_para_string_animacao(EstadoSistema estado) {
    switch (estado) {
        case ESTADO_NORMAL:       return "Icone OK (v)";
        case ESTADO_TEMP_ALTA:    return "Icone Alerta (!)";
        case ESTADO_TEMP_BAIXA:   return "Animacao Chuva";
        case ESTADO_UMID_ALTA:    return "Animacao Chuva";
        case ESTADO_UMID_BAIXA:   return "Icone Alerta (!)";
        case ESTADO_PRESS_ALTA:   return "Quadrado";
        case ESTADO_PRESS_BAIXA:  return "Icone Erro (X)";
        default:                  return "Nenhuma";
    }
}

/* ---------------- Função de Controle dos LEDs RGB -------------- */
/**
 * @brief Atualiza o LED RGB com base no estado atual do sistema.
 * @param estado O estado atual do sistema.
 */
void atualizar_leds_rgb(EstadoSistema estado) {
    // Desliga todos os LEDs primeiro
    gpio_put(LED_VERDE_PIN, 0);
    gpio_put(LED_AZUL_PIN, 0);
    gpio_put(LED_VERMELHO_PIN, 0);
    
    switch (estado) {
        case ESTADO_NORMAL:
            gpio_put(LED_VERDE_PIN, 1); // Verde
            break;
        case ESTADO_TEMP_ALTA:
            gpio_put(LED_VERMELHO_PIN, 1); // Vermelho
            break;
        case ESTADO_TEMP_BAIXA:
            gpio_put(LED_AZUL_PIN, 1); // Azul
            break;
        case ESTADO_UMID_ALTA:
            gpio_put(LED_AZUL_PIN, 1);    // Roxo (Azul + Vermelho)
            gpio_put(LED_VERMELHO_PIN, 1);
            break;
        case ESTADO_UMID_BAIXA:
            gpio_put(LED_VERDE_PIN, 1);   // Amarelo (Verde + Vermelho)
            gpio_put(LED_VERMELHO_PIN, 1);
            break;
        case ESTADO_PRESS_ALTA:
            gpio_put(LED_VERDE_PIN, 1);   // Branco (Verde + Azul + Vermelho)
            gpio_put(LED_AZUL_PIN, 1);
            gpio_put(LED_VERMELHO_PIN, 1);
            break;
        case ESTADO_PRESS_BAIXA:
            // Para pressão baixa, o LED RGB fica desligado, e a matriz fica cinza.
            break;
    }
}

/* ================================================================ */
/* -------------------------- FUNÇÃO MAIN ------------------------- */
/* ================================================================ */
int main() {
    stdio_init_all();
    inicializar_matriz_led(); // <-- INICIALIZA A MATRIZ DE LED
    sleep_ms(2000);

    ssd1306_t tela;
    struct bmp280_calib_param parametros_bmp;
    uint64_t proxima_leitura = 0;

    configurar_perifericos(&tela, &parametros_bmp);
    configurar_botoes();
    configurar_joystick();
    configurar_leds_rgb();
    configurar_wifi(&tela);

    while (true) {
        if (wifi_conectado) {
            cyw43_arch_poll();
        }

        // Bloco de leitura e atualização de estado (a cada 2 segundos)
        if (time_us_64() >= proxima_leitura) {
            ler_e_exibir_dados(&parametros_bmp, &temperatura_aht, &temperatura_bmp, &temperatura_media, &umidade, &pressao);
            proxima_leitura = time_us_64() + INTERVALO_LEITURA_MS * 1000;
            
            // Atualiza buffers de gráficos
            buffer_temperaturas[indice_buffer] = temperatura_media;
            buffer_umidade[indice_buffer] = umidade;
            buffer_pressao[indice_buffer] = pressao / 100.0f;
            indice_buffer = (indice_buffer + 1) % TAMANHO_GRAFICO;
            if (contador_buffer < TAMANHO_GRAFICO) contador_buffer++;
            
            historico_temp_media[indice_historico] = temperatura_media;
            historico_umidade[indice_historico] = umidade;
            historico_pressao[indice_historico] = pressao / 100.0f;
            indice_historico = (indice_historico + 1) % TAMANHO_HISTORICO_WEB;
            if (contador_historico < TAMANHO_HISTORICO_WEB) contador_historico++;
            
            // --- NOVA LÓGICA DE ATUALIZAÇÃO ---
            estado_global = obter_estado_sistema(); // Determina o estado atual
            atualizar_leds_rgb(estado_global);      // Atualiza o LED RGB simples
            
            // Sinaliza para a tela OLED ser atualizada, se necessário
            if (tela_atual == TELA_VALORES || tela_atual == TELA_GRAFICO || tela_atual == TELA_UMIDADE || tela_atual == TELA_PRESSAO || tela_atual == TELA_LED_STATUS)
                atualizar_tela_flag = true;
        }

        // --- ATUALIZAÇÃO CONTÍNUA DA MATRIZ ---
        // Chamado em cada iteração do loop para permitir animações fluidas
        atualizar_matriz_pelo_estado(estado_global);

        // Atualiza a tela OLED se a flag estiver ativa
        if (atualizar_tela_flag) {
            atualizar_tela(&tela, temperatura_aht, temperatura_bmp, temperatura_media, umidade, pressao);
            atualizar_tela_flag = false;
        }

        callback_joystick();
        sleep_ms(10);
    }
    return 0;
}

/* ---------------- Inicialização (sem alterações) --------------- */
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

void configurar_leds_rgb(void) {
    gpio_init(LED_VERDE_PIN);
    gpio_set_dir(LED_VERDE_PIN, GPIO_OUT);
    gpio_put(LED_VERDE_PIN, 0);
    
    gpio_init(LED_AZUL_PIN);
    gpio_set_dir(LED_AZUL_PIN, GPIO_OUT);
    gpio_put(LED_AZUL_PIN, 0);
    
    gpio_init(LED_VERMELHO_PIN);
    gpio_set_dir(LED_VERMELHO_PIN, GPIO_OUT);
    gpio_put(LED_VERMELHO_PIN, 0);
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

/* ---------------- Callbacks de Interrupção (sem alterações) ---- */
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
    ssd1306_draw_string(tela, buffer, 0, 12, false);

    snprintf(buffer, sizeof(buffer), "WiFi: %s", wifi_conectado ? "OK" : "ERRO");
    ssd1306_draw_string(tela, buffer, 0, 22, false);

    // Status da Temperatura
    const char *status_temp;
    if (temperatura_media < temperatura_limite_inferior) {
        status_temp = "Abaixo";
    } else if (temperatura_media > temperatura_limite_superior) {
        status_temp = "Acima";
    } else {
        status_temp = "Normal";
    }
    snprintf(buffer, sizeof(buffer), "Est.Temp: %s", status_temp);
    ssd1306_draw_string(tela, buffer, 0, 32, false);

    // Status da Umidade
    const char *status_umid;
    if (umidade < umidade_limite_inferior) {
        status_umid = "Abaixo";
    } else if (umidade > umidade_limite_superior) {
        status_umid = "Acima";
    } else {
        status_umid = "Normal";
    }
    snprintf(buffer, sizeof(buffer), "Est.Umid: %s", status_umid);
    ssd1306_draw_string(tela, buffer, 0, 42, false);

    // Status da Pressão
    const char *status_press;
    float pressao_hpa = pressao / 100.0f;
    if (pressao_hpa < pressao_limite_inferior) {
        status_press = "Abaixo";
    } else if (pressao_hpa > pressao_limite_superior) {
        status_press = "Acima";
    } else {
        status_press = "Normal";
    }
    snprintf(buffer, sizeof(buffer), "Est.Press: %s", status_press);
    ssd1306_draw_string(tela, buffer, 0, 52, false);

    ssd1306_send_data(tela);
}

/**
 * @brief (REVERTIDA) Mostra o status do LED RGB na tela OLED.
 */
void mostrar_tela_led_status(ssd1306_t *tela) {
    ssd1306_fill(tela, 0);
    
    // Título centralizado
    const char *titulo = "Status LED RGB";
    ssd1306_draw_string(tela, titulo, (TELA_LARGURA - strlen(titulo) * 8) / 2, 0, false);
    
    // Linha separadora
    ssd1306_hline(tela, 0, TELA_LARGURA, 12, true);
    
    // Busca o estado atual e converte para a string da cor do LED
    const char *cor_atual = estado_para_string_cor(estado_global);
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "Cor Atual:");
    ssd1306_draw_string(tela, buffer, 10, 25, false);
    
    // Destaca a cor atual, centralizando-a
    // Trata o caso especial da pressão baixa, onde o LED RGB fica desligado.
    const char* cor_display = cor_atual;
    if (estado_global == ESTADO_PRESS_BAIXA) {
        cor_display = "Desligado";
    }
    ssd1306_draw_string(tela, cor_display, (TELA_LARGURA - strlen(cor_display) * 8) / 2, 40, false);
    
    // Informação adicional
    const char *info = "Baseado nos sensores";
    ssd1306_draw_string(tela, info, (TELA_LARGURA - strlen(info) * 6) / 2, 55, true);
    
    ssd1306_send_data(tela);
}

// Funções de gráfico (sem alterações)
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
        case TELA_VALORES: mostrar_tela_valores(tela, temp_aht, temp_bmp, temp_media, umid, press); break;
        case TELA_CONEXAO: mostrar_tela_conexao(tela); break;
        case TELA_GRAFICO: mostrar_tela_grafico(tela); break;
        case TELA_UMIDADE: mostrar_tela_umidade(tela); break;
        case TELA_PRESSAO: mostrar_tela_pressao(tela); break;
        case TELA_LED_STATUS: mostrar_tela_led_status(tela); break;
    }
}

void ler_e_exibir_dados(struct bmp280_calib_param *parametros, float *temp_aht, float *temp_bmp, float *temp_media, float *umid, float *press) {
    AHT20_Data dados_aht;
    aht20_read(I2C_SENSORES_PORT, &dados_aht);
    *temp_aht = dados_aht.temperature;
    *umid = dados_aht.humidity;
    int32_t temp_raw, press_raw;
    bmp280_read_raw(I2C_SENSORES_PORT, &temp_raw, &press_raw);
    int32_t temp_convertida = bmp280_convert_temp(temp_raw, parametros);
    int32_t press_convertida = bmp280_convert_pressure(press_raw, temp_raw, parametros);
    *temp_bmp = temp_convertida / 100.0f;
    *press = press_convertida;
    *temp_media = (*temp_aht + *temp_bmp) / 2.0f;
}
