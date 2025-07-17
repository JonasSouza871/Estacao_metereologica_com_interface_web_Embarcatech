/* PicoAtmos – Versão final com formatação de eixo Y
 * --------------------------------------------------
 * • Gráfico com janela de 60 segundos (30 amostras @ 2s/amostra).
 * • Rótulos do eixo X em 0, 30s e 60s.
 * • Rótulos do eixo Y mostram ".0" apenas para números não inteiros.
 * • Joystick: “cima” → aproxima (zoom‑in); “baixo” → afasta.
 *
 * Necessita: -lm na vinculação (powf, log10f)
 */

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

/* ---- Hardware -------------------------------------------------- */
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

/* ---- Estado / tempo ------------------------------------------- */
#define TELA_ABERTURA           0
#define TELA_VALORES            1
#define TELA_CONEXAO            2
#define TELA_GRAFICO            3
#define DEBOUNCE_MS             250
#define INTERVALO_LEITURA_MS    2000
#define TAMANHO_GRAFICO         30 // 60 segundos / 2s por leitura

/* ---- Variáveis Globais ---------------------------------------- */
volatile uint8_t tela_atual = TELA_ABERTURA;
volatile bool    precisa_atualizar_tela = true;
volatile float   zoom_factor = 1.0f;

static uint64_t  ultimo_zoom_ms = 0;
const  uint32_t  ZOOM_DEBOUNCE_MS = 120;

float buffer_temperaturas[TAMANHO_GRAFICO];
int   indice_buffer   = 0;
int   contador_buffer = 0;

/* ---- Protótipos ---------------------------------------------- */
void configurar_perifericos(ssd1306_t *, struct bmp280_calib_param *);
void configurar_botoes(void);
void configurar_joystick(void);
void mostrar_tela_abertura(ssd1306_t *);
void mostrar_tela_valores(ssd1306_t *, float,float,float,float,float);
void mostrar_tela_conexao(ssd1306_t *);
void mostrar_tela_grafico(ssd1306_t *);
void atualizar_tela(ssd1306_t *, float,float,float,float,float);
void ler_e_exibir_dados(struct bmp280_calib_param *, float*,float*,float*,float*,float*);
void botoes_callback(uint, uint32_t);
void joystick_callback(void);

/* ================================================================ */
int main() {
    stdio_init_all();
    sleep_ms(2000);

    ssd1306_t tela;
    struct bmp280_calib_param bmp_params;
    float t_aht=0, t_bmp=0, t_med=0, umid=0, press=0;
    uint64_t proxima_leitura = 0;

    configurar_perifericos(&tela,&bmp_params);
    configurar_botoes();
    configurar_joystick();

    mostrar_tela_abertura(&tela);
    sleep_ms(2500);
    tela_atual = TELA_VALORES; // Inicia na tela de valores após abertura
    precisa_atualizar_tela = true;

    while (true) {
        if (time_us_64() >= proxima_leitura) {
            ler_e_exibir_dados(&bmp_params,&t_aht,&t_bmp,&t_med,&umid,&press);
            proxima_leitura = time_us_64() + INTERVALO_LEITURA_MS*1000;

            buffer_temperaturas[indice_buffer] = t_med;
            indice_buffer = (indice_buffer+1)%TAMANHO_GRAFICO;
            if (contador_buffer < TAMANHO_GRAFICO) contador_buffer++;

            if (tela_atual==TELA_VALORES || tela_atual==TELA_GRAFICO)
                precisa_atualizar_tela = true;
        }
        if (precisa_atualizar_tela) {
            atualizar_tela(&tela,t_aht,t_bmp,t_med,umid,press);
            precisa_atualizar_tela = false;
        }
        joystick_callback();
        sleep_ms(10);
    }
    return 0;
}

/* ---------------- Inicialização -------------------------------- */
void configurar_perifericos(ssd1306_t *tela, struct bmp280_calib_param *p) {
    i2c_init(I2C_SENSORES_PORT,100*1000);
    gpio_set_function(I2C_SENSORES_SDA_PIN,GPIO_FUNC_I2C);
    gpio_set_function(I2C_SENSORES_SCL_PIN,GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SENSORES_SDA_PIN); gpio_pull_up(I2C_SENSORES_SCL_PIN);

    i2c_init(I2C_TELA_PORT,400*1000);
    gpio_set_function(I2C_TELA_SDA_PIN,GPIO_FUNC_I2C);
    gpio_set_function(I2C_TELA_SCL_PIN,GPIO_FUNC_I2C);
    gpio_pull_up(I2C_TELA_SDA_PIN); gpio_pull_up(I2C_TELA_SCL_PIN);

    ssd1306_init(tela,TELA_LARGURA,TELA_ALTURA,false,TELA_ENDERECO,I2C_TELA_PORT);
    ssd1306_config(tela);

    aht20_init(I2C_SENSORES_PORT);
    bmp280_init(I2C_SENSORES_PORT);
    bmp280_get_calib_params(I2C_SENSORES_PORT,p);
}

void configurar_botoes(void){
    gpio_init(BOTAO_A_PIN); gpio_set_dir(BOTAO_A_PIN,GPIO_IN); gpio_pull_up(BOTAO_A_PIN);
    gpio_init(BOTAO_B_PIN); gpio_set_dir(BOTAO_B_PIN,GPIO_IN); gpio_pull_up(BOTAO_B_PIN);
    gpio_set_irq_enabled_with_callback(BOTAO_A_PIN,GPIO_IRQ_EDGE_FALL,true,&botoes_callback);
    gpio_set_irq_enabled_with_callback(BOTAO_B_PIN,GPIO_IRQ_EDGE_FALL,true,&botoes_callback);
}

void configurar_joystick(void){
    adc_init();
    adc_gpio_init(JOYSTICK_PIN);
    adc_select_input(0); // Canal 0 corresponde ao GPIO26
}

/* ---------------- Botões --------------------------------------- */
void botoes_callback(uint gpio,uint32_t events){
    static uint64_t la=0, lb=0;
    uint64_t now=time_us_64();
    if(events & GPIO_IRQ_EDGE_FALL){
        if(gpio==BOTAO_A_PIN && now-la>DEBOUNCE_MS*1000){
            tela_atual=(tela_atual+1)%4; precisa_atualizar_tela=true; la=now;
        }
        if(gpio==BOTAO_B_PIN && now-lb>DEBOUNCE_MS*1000){
            tela_atual=(tela_atual==0)?3:tela_atual-1; precisa_atualizar_tela=true; lb=now;
        }
    }
}

/* ---------------- Joystick / Zoom ------------------------------ */
void joystick_callback(void){
    const uint16_t DEAD_LOW=1500, DEAD_HIGH=2500;
    uint16_t v = adc_read();
    uint64_t now = to_ms_since_boot(get_absolute_time());
    if(now-ultimo_zoom_ms<ZOOM_DEBOUNCE_MS) return;

    bool mudou=false;
    /* Nesta placa: valor ALTO = alavanca para CIMA  */
    if(v > DEAD_HIGH && zoom_factor < 4.0f){        /* cima → zoom‑in */
        zoom_factor += 0.10f; mudou=true;
    } else if(v < DEAD_LOW && zoom_factor > 0.25f){   /* baixo → zoom‑out */
        zoom_factor -= 0.10f; mudou=true;
    }
    if(mudou){ ultimo_zoom_ms=now; precisa_atualizar_tela=true; }
}

/* ---------------- Telas ---------------------------------------- */
void mostrar_tela_abertura(ssd1306_t *t){
    ssd1306_fill(t,0);
    ssd1306_rect(t,5,5,118,54,1,false);
    const char*txt="PicoAtmos";
    ssd1306_draw_string(t,txt,(TELA_LARGURA-strlen(txt)*8)/2,28,false);
    const char*sb="Iniciando...";
    ssd1306_draw_string(t,sb,(TELA_LARGURA-strlen(sb)*6)/2,45,true);
    ssd1306_send_data(t);
}

void mostrar_tela_valores(ssd1306_t *t,float a,float b,float m,float u,float p){
    ssd1306_fill(t,0);
    const char*hd="Valores Medidos";
    ssd1306_draw_string(t,hd,(TELA_LARGURA-strlen(hd)*8)/2,0,false);
    char buf[32];
    snprintf(buf,sizeof(buf),"TempAHT: %.1fC",a); ssd1306_draw_string(t,buf,0,12,false);
    snprintf(buf,sizeof(buf),"TempBMP: %.1fC",b); ssd1306_draw_string(t,buf,0,22,false);
    snprintf(buf,sizeof(buf),"TempMed: %.1fC",m); ssd1306_draw_string(t,buf,0,32,false);
    snprintf(buf,sizeof(buf),"Umidade: %.1f%%",u);ssd1306_draw_string(t,buf,0,42,false);
    snprintf(buf,sizeof(buf),"Pressao: %.1fhPa",p/100.0); ssd1306_draw_string(t,buf,0,52,false);
    ssd1306_send_data(t);
}

void mostrar_tela_conexao(ssd1306_t *t){
    ssd1306_fill(t,0);
    const char*hd="Conexao";
    ssd1306_draw_string(t,hd,(TELA_LARGURA-strlen(hd)*8)/2,0,false);
    ssd1306_draw_string(t,"TempSet:",0,10,false);
    ssd1306_draw_string(t,"UmidSet:",0,20,false);
    ssd1306_draw_string(t,"PresSet:",0,30,false);
    ssd1306_draw_string(t,"IP:",0,40,false);
    ssd1306_draw_string(t,"Status:",0,50,false);
    ssd1306_send_data(t);
}


/* ---- Gráfico com eixos e rótulos corrigidos ------------------- */
void mostrar_tela_grafico(ssd1306_t *t) {
    ssd1306_fill(t, 0);

    /* Área do gráfico */
    const uint8_t gx = 20;  // X da origem (aumentado para dar espaço aos rótulos Y)
    const uint8_t gy = 52;  // Y da linha X (eixo horizontal), movido para cima
    const uint8_t H  = 40;  // Altura útil do gráfico
    const uint8_t W  = 105; // Largura útil do gráfico

    /* Título */
    const char *ttl = "Temp media";
    ssd1306_draw_string(t, ttl, (TELA_LARGURA - strlen(ttl) * 8) / 2, 0, false);

    if (contador_buffer == 0) {
        ssd1306_draw_string(t, "Aguardando dados...", 8, 30, false);
        ssd1306_send_data(t);
        return;
    }

    /* --- Encontra faixa de dados (min/max) ---------------------- */
    float vmin = buffer_temperaturas[0], vmax = vmin;
    // Itera sobre os dados válidos no buffer circular
    for (int i = 0; i < contador_buffer; ++i) {
        int idx = (indice_buffer - contador_buffer + i + TAMANHO_GRAFICO) % TAMANHO_GRAFICO;
        float v  = buffer_temperaturas[idx];
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    // Garante uma faixa vertical mínima de 2 graus para estabilizar o eixo Y
    if (vmax - vmin < 2.0f) {
        float mid_val = (vmax + vmin) / 2.0f;
        vmin = mid_val - 1.0f;
        vmax = mid_val + 1.0f;
    }


    /* Aplica zoom (centro da faixa de dados como pivô) */
    float vis_range = (vmax - vmin) / zoom_factor;
    float mid = (vmax + vmin) * 0.5f;
    float ymin = mid - vis_range * 0.5f;
    float ymax = mid + vis_range * 0.5f;

    /* Calcula passo "bonito" (1-2-5) para as marcações do eixo Y */
    float rough_step = vis_range / 3.0f; // Tenta ter ~3 divisões
    float pwr  = powf(10.0f, floorf(log10f(rough_step)));
    float mant = rough_step / pwr;
    float step = (mant < 1.5f) ? 1.0f : (mant < 3.5f) ? 2.0f : (mant < 7.5f) ? 5.0f : 10.0f;
    step *= pwr;
    if (step < 0.1f) step = 0.1f; // Evita passos muito pequenos

    /* Ajusta limites para o múltiplo do passo mais próximo */
    float ymax_tick = ceilf (ymax / step) * step;
    float ymin_tick = floorf(ymin / step) * step;
    vis_range = ymax_tick - ymin_tick;
    if (vis_range < 0.1f) vis_range = 0.1f; // Garante faixa mínima

    /* --- Desenha eixos ------------------------------------------ */
    ssd1306_hline(t, gx, gx + W, gy, true);      // eixo X
    ssd1306_vline(t, gx, gy - H, gy, true);      // eixo Y

    /* --- Marcas / rótulos Y (máx. 4) ---------------------------- */
    char buf[8];
    int ticks = 0;
    for (float tv = ymax_tick; tv >= ymin_tick - (step*0.1f); tv -= step) {
        if (ticks++ >= 4) break;
        // Mapeia o valor da temperatura para a coordenada Y na tela
        uint8_t y = gy - (uint8_t)(((tv - ymin_tick) / vis_range) * H);
        if (y > gy || y < (gy - H)) continue; // Não desenha fora da área

        ssd1306_hline(t, gx - 2, gx, y, true);

        // --- Formatação condicional para os rótulos ---
        // Se for um número inteiro (ex: 27.0), mostra como "27"
        // Se tiver decimal (ex: 27.5), mostra como "27.5"
        if (tv == floorf(tv)) {
            snprintf(buf, sizeof(buf), "%.0f", tv);
        } else {
            snprintf(buf, sizeof(buf), "%.1f", tv);
        }
        
        // Usa a fonte padrão (false) para melhor legibilidade
        ssd1306_draw_string(t, buf, 0, y - 4, false);
    }

    /* --- Marcas / rótulos X (janela de 60 segundos) ------------- */
    // Ticks em 0s, 30s e 60s
    ssd1306_vline(t, gx,       gy, gy + 2, true); // 0s
    ssd1306_vline(t, gx + W/2, gy, gy + 2, true); // 30s
    ssd1306_vline(t, gx + W,   gy, gy + 2, true); // 60s

    // Rótulos (posições ajustadas manualmente para bom alinhamento)
    ssd1306_draw_string(t, "0",   gx - 3,          gy + 5, false);
    ssd1306_draw_string(t, "30s", gx + W/2 - 10,   gy + 5, false);
    ssd1306_draw_string(t, "60s", gx + W - 18,     gy + 5, false);

    /* --- Traça a curva da temperatura --------------------------- */
    if (contador_buffer > 1) {
        for (int i = 0; i < contador_buffer - 1; ++i) {
            int i_atual = (indice_buffer - contador_buffer + i + TAMANHO_GRAFICO) % TAMANHO_GRAFICO;
            int i_prox  = (i_atual + 1) % TAMANHO_GRAFICO;

            float v1 = buffer_temperaturas[i_atual];
            float v2 = buffer_temperaturas[i_prox];

            // Mapeia os pontos para as coordenadas X e Y
            uint8_t x1 = gx + (i * W) / (TAMANHO_GRAFICO - 1);
            uint8_t y1 = gy - (uint8_t)(((v1 - ymin_tick) / vis_range) * H);
            uint8_t x2 = gx + ((i + 1) * W) / (TAMANHO_GRAFICO - 1);
            uint8_t y2 = gy - (uint8_t)(((v2 - ymin_tick) / vis_range) * H);

            ssd1306_line(t, x1, y1, x2, y2, true);
        }
    }

    ssd1306_send_data(t);
}
/* ---------------------------------------------------------------- */

void atualizar_tela(ssd1306_t *t,float a,float b,float m,float u,float p){
    switch(tela_atual){
        case TELA_ABERTURA: mostrar_tela_abertura(t); break;
        case TELA_VALORES : mostrar_tela_valores(t,a,b,m,u,p); break;
        case TELA_CONEXAO : mostrar_tela_conexao(t); break;
        case TELA_GRAFICO : mostrar_tela_grafico(t); break;
    }
}

/* ---------------- Sensores ------------------------------------- */
void ler_e_exibir_dados(struct bmp280_calib_param *p,float *a,float *b,float *m,float *u,float *prs){
    AHT20_Data d; aht20_read(I2C_SENSORES_PORT,&d);
    *a=d.temperature; *u=d.humidity;
    int32_t rt,rp; bmp280_read_raw(I2C_SENSORES_PORT,&rt,&rp);
    int32_t tc=bmp280_convert_temp(rt,p);
    int32_t pc=bmp280_convert_pressure(rp,rt,p);
    *b=tc/100.0f; *prs=pc; *m=(*a+*b)/2.0f;
}
