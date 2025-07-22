#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "pico/stdlib.h"      
#include "pico/cyw43_arch.h"   // Biblioteca para WiFi do Pico W
#include "lwipopts.h"         // Configurações da pilha TCP/IP
#include "lwipopts_examples_common.h"
#include "hardware/i2c.h"     // Interface I2C para comunicação com sensores
#include "hardware/gpio.h"      
#include "hardware/adc.h"     // Conversor analógico-digital
#include "hardware/irq.h"     // Sistema de interrupções
#include "hardware/pwm.h"     // Modulação por largura de pulso (para buzzer)
#include "lwip/tcp.h"         // Protocolo TCP para servidor web
// Inclusões dos arquivos específicos do projeto
#include "aht20.h"            // Driver do sensor de temperatura/umidade AHT20
#include "bmp280.h"           // Driver do sensor de pressão/temperatura BMP280
#include "ssd1306.h"          // Driver do display OLED
#include "font.h"             // Fonte para exibição de texto
#include "matriz_led.h"       // Controle da matriz de LEDs
#include "html.h"             // Páginas web armazenadas em memória

/* =================== CONFIGURAÇÕES DE HARDWARE =================== */
// Configuração do barramento I2C para os sensores (AHT20 e BMP280)
#define I2C_SENSORES_PORT i2c0       // Usa o barramento I2C número 0
#define I2C_SENSORES_SDA_PIN 0       // Pino de dados (SDA) do I2C dos sensores
#define I2C_SENSORES_SCL_PIN 1       // Pino de clock (SCL) do I2C dos sensores

// Configuração do barramento I2C para o display OLED
#define I2C_DISPLAY_PORT i2c1        // Usa o barramento I2C número 1 (separado dos sensores)
#define I2C_DISPLAY_SDA_PIN 14       // Pino de dados do display
#define I2C_DISPLAY_SCL_PIN 15       // Pino de clock do display
#define ENDERECO_DISPLAY 0x3C        // Endereço I2C do display (hexadecimal)
#define LARGURA_DISPLAY 128          // Resolução horizontal do display em pixels
#define ALTURA_DISPLAY 64            // Resolução vertical do display em pixels

// Configuração dos botões de navegação
#define BOTAO_AVANCAR_PIN 5          // Botão para avançar telas (com pull-up interno)
#define BOTAO_VOLTAR_PIN 6           // Botão para voltar telas (com pull-up interno)

// Configuração do joystick analógico (usado para zoom nos gráficos)
#define JOYSTICK_ZOOM_PIN 26         // Pino ADC do joystick (GPIO 26 = ADC0)

// Configuração dos componentes de alerta
#define BUZZER_ALARME_PIN 10         // Pino PWM para controle do buzzer
#define LED_VERDE_PIN 11             // LED verde do sistema RGB
#define LED_AZUL_PIN 12              // LED azul do sistema RGB  
#define LED_VERMELHO_PIN 13          // LED vermelho do sistema RGB

/* =================== CONFIGURAÇÕES DE REDE =================== */
#define NOME_WIFI "SUA_REDE_WIFI"      // Nome da rede WiFi para conexão
#define SENHA_WIFI "SUA_SENHA"        // Senha da rede WiFi

/* =================== DEFINIÇÕES DAS TELAS DO SISTEMA =================== */
// Enumera todas as telas disponíveis no sistema de navegação
#define TELA_INICIAL 0               // Tela de boas-vindas/abertura
#define TELA_DADOS_SENSORES 1        // Mostra valores atuais dos sensores
#define TELA_STATUS_REDE 2           // Status de conectividade e sistema
#define TELA_GRAFICO_TEMP 3          // Gráfico da temperatura ao longo do tempo
#define TELA_GRAFICO_UMIDADE 4       // Gráfico da umidade ao longo do tempo
#define TELA_GRAFICO_PRESSAO 5       // Gráfico da pressão ao longo do tempo
#define TELA_STATUS_LEDS 6           // Status atual dos LEDs indicadores
#define TOTAL_TELAS 7                // Número total de telas disponíveis

/* =================== CONFIGURAÇÕES DE TEMPORIZAÇÃO =================== */
#define TEMPO_DEBOUNCE_MS 250        // Tempo para evitar múltiplos cliques dos botões (ms)
#define INTERVALO_LEITURA_MS 2000    // Intervalo entre leituras dos sensores (2 segundos)
#define TAMANHO_BUFFER_GRAFICO 30    // Quantos pontos são armazenados para cada gráfico
#define TAMANHO_HISTORICO_WEB 100    // Quantos pontos ficam disponíveis via web
#define TEMPO_DEBOUNCE_ZOOM_MS 120   // Tempo de debounce para o joystick de zoom

/* =================== LIMITES DE MONITORAMENTO =================== */
// Estes valores definem quando o sistema deve gerar alertas
// Podem ser alterados via interface web
float limite_temp_min = 20.0f;       // Temperatura mínima aceitável (°C)
float limite_temp_max = 30.0f;       // Temperatura máxima aceitável (°C)
float limite_umid_min = 40.0f;       // Umidade mínima aceitável (%)
float limite_umid_max = 80.0f;       // Umidade máxima aceitável (%)
float limite_press_min = 900.0f;     // Pressão mínima aceitável (hPa)
float limite_press_max = 1000.0f;    // Pressão máxima aceitável (hPa)

/* =================== CALIBRAÇÃO DOS SENSORES =================== */
// Valores de ajuste para corrigir erros sistemáticos dos sensores
// Podem ser configurados via interface web para melhorar a precisão
float ajuste_temp_aht = 0.0f;        // Ajuste para temperatura do sensor AHT20
float ajuste_temp_bmp = 0.0f;        // Ajuste para temperatura do sensor BMP280
float ajuste_umidade = 0.0f;         // Ajuste para umidade do AHT20
float ajuste_pressao = 0.0f;         // Ajuste para pressão do BMP280

/* =================== VARIÁVEIS DE CONTROLE DO SISTEMA =================== */
bool wifi_conectado = false;         // Flag indicando se WiFi está conectado
char endereco_ip[24] = "Desconectado"; // String com o IP atual ou status de erro

// Controle da interface do usuário
volatile uint8_t tela_ativa = TELA_INICIAL;       // Qual tela está sendo exibida atualmente
volatile bool flag_atualizar_display = true;      // Sinaliza quando o display precisa ser atualizado
volatile float fator_zoom_temp = 1.0f;            // Nível de zoom do gráfico de temperatura
volatile float fator_zoom_umid = 1.0f;            // Nível de zoom do gráfico de umidade  
volatile float fator_zoom_press = 1.0f;           // Nível de zoom do gráfico de pressão
static uint64_t ultimo_zoom_ms = 0;               // Timestamp da última ação de zoom
volatile EstadoSistema estado_atual = ESTADO_NORMAL; // Estado atual do sistema para alertas

/* =================== BUFFERS DE DADOS =================== */
// Arrays circulares para armazenar histórico das leituras dos sensores
float historico_temp[TAMANHO_BUFFER_GRAFICO];     // Histórico de temperatura para gráficos
float historico_umid[TAMANHO_BUFFER_GRAFICO];     // Histórico de umidade para gráficos
float historico_press[TAMANHO_BUFFER_GRAFICO];    // Histórico de pressão para gráficos
int indice_circular = 0;                          // Índice atual no buffer circular
int contador_amostras = 0;                        // Quantas amostras já foram coletadas

// Buffers maiores para disponibilizar dados via web
float dados_web_temp[TAMANHO_HISTORICO_WEB];      // Histórico temperatura para web
float dados_web_umid[TAMANHO_HISTORICO_WEB];      // Histórico umidade para web  
float dados_web_press[TAMANHO_HISTORICO_WEB];     // Histórico pressão para web
int indice_web = 0;                               // Índice atual no buffer web
int contador_web = 0;                             // Quantas amostras web foram coletadas

/* =================== LEITURAS ATUAIS DOS SENSORES =================== */
float temp_aht = 0;      // Última temperatura lida do sensor AHT20 (°C)
float temp_bmp = 0;      // Última temperatura lida do sensor BMP280 (°C)
float temp_media = 0;    // Média das temperaturas dos dois sensores (°C)
float umidade_atual = 0; // Última umidade lida do AHT20 (%)
float pressao_atual = 0; // Última pressão lida do BMP280 (Pa)

/* =================== ESTRUTURA PARA SERVIDOR HTTP =================== */
// Esta estrutura armazena o estado de cada conexão HTTP
struct estado_http {
    char resposta[12288];  // Buffer para montar a resposta HTTP (12KB)
    size_t tamanho;        // Tamanho total da resposta em bytes
    size_t enviado;        // Quantos bytes já foram enviados
};

/* =================== PROTÓTIPOS DAS FUNÇÕES =================== */
// Funções de inicialização do hardware
void inicializar_hardware_completo(ssd1306_t *, struct bmp280_calib_param *);
void configurar_botoes_navegacao(void);
void configurar_joystick_zoom(void);
void configurar_leds_status(void);
void inicializar_conexao_wifi(ssd1306_t *);
void iniciar_servidor_web(void);

// Funções para desenho de cada tela
void exibir_tela_inicial(ssd1306_t *);
void exibir_dados_sensores(ssd1306_t *, float, float, float, float, float);
void exibir_status_conexao(ssd1306_t *);
void exibir_grafico_temperatura(ssd1306_t *);
void exibir_grafico_umidade(ssd1306_t *);
void exibir_grafico_pressao(ssd1306_t *);
void exibir_status_led_rgb(ssd1306_t *);

// Funções de controle principal
void atualizar_display_principal(ssd1306_t *, float, float, float, float, float);
void coletar_dados_todos_sensores(struct bmp280_calib_param *, float *, float *, float *, float *, float *);

// Funções de callback (chamadas por interrupções)
void processar_botoes_pressionados(uint, uint32_t);
void processar_movimento_joystick(void);

// Funções de lógica de estados e alertas
EstadoSistema verificar_estado_atual(void);
void atualizar_indicadores_led(EstadoSistema);
const char* obter_cor_estado_texto(EstadoSistema);
const char* obter_animacao_estado_texto(EstadoSistema);

// Funções para alertas sonoros
void configurar_buzzer_alertas(void);
void processar_alertas_sonoros(EstadoSistema);
void definir_frequencia_buzzer(uint, float);

/* =================== FUNÇÕES DO SERVIDOR WEB =================== */
// Função chamada quando dados são enviados com sucesso via TCP
// Serve para controlar o progresso do envio e fechar a conexão quando terminar
static err_t callback_envio_http(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    struct estado_http *hs = (struct estado_http *)arg; // Recupera estado da conexão
    hs->enviado += len;                                 // Atualiza quantos bytes foram enviados
    
    // Se enviou tudo, fecha a conexão e libera memória
    if (hs->enviado >= hs->tamanho) {
        tcp_close(tpcb);   // Fecha conexão TCP
        free(hs);          // Libera memória alocada para esta conexão
    }
    return ERR_OK; // Retorna sucesso
}

// Função principal que processa requisições HTTP recebidas
// Analisa a URL requisitada e gera a resposta apropriada
static err_t callback_recepcao_http(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    // Se não há dados, cliente fechou conexão
    if (!p) { 
        tcp_close(tpcb); 
        return ERR_OK; 
    }
    
    char *requisicao = (char *)p->payload; // Extrai texto da requisição HTTP
    
    // Aloca memória para estado desta conexão
    struct estado_http *hs = malloc(sizeof(struct estado_http));
    if (!hs) { // Se não conseguiu alocar, fecha conexão
        pbuf_free(p); 
        tcp_close(tpcb); 
        return ERR_MEM; 
    }
    hs->enviado = 0; // Inicializa contador de bytes enviados
    
    // Analisa qual endpoint foi requisitado e gera resposta apropriada
    if (strstr(requisicao, "GET /dados")) {
        // Endpoint que retorna dados dos sensores em formato JSON
        // Usado pela interface web para atualizar valores em tempo real
        char payload_json[768];
        int tam_json = snprintf(payload_json, sizeof(payload_json),
            "{\"temp_aht\":%.2f,\"temp_bmp\":%.2f,\"temp_media\":%.2f,\"umidade\":%.2f,\"pressao\":%.2f,"
            "\"temp_min\":%.2f,\"temp_max\":%.2f,\"umid_min\":%.2f,\"umid_max\":%.2f,"
            "\"press_min\":%.2f,\"press_max\":%.2f,"
            "\"offset_temp_aht\":%.2f,\"offset_temp_bmp\":%.2f,\"offset_umid\":%.2f,\"offset_press\":%.2f}",
            temp_aht, temp_bmp, temp_media, umidade_atual, pressao_atual / 100.0f,
            limite_temp_min, limite_temp_max, limite_umid_min, limite_umid_max,
            limite_press_min, limite_press_max, 
            ajuste_temp_aht, ajuste_temp_bmp, ajuste_umidade, ajuste_pressao);
            
        // Monta cabeçalho HTTP + JSON
        hs->tamanho = snprintf(hs->resposta, sizeof(hs->resposta),
            "HTTP/1.1 200 OK\r\n"                  // Status de sucesso
            "Content-Type: application/json\r\n"   // Tipo de conteúdo
            "Content-Length: %d\r\n"               // Tamanho do conteúdo
            "Connection: close\r\n"                // Fecha conexão após envio
            "\r\n"                                 // Linha em branco (fim do cabeçalho)
            "%s", tam_json, payload_json);         // Conteúdo JSON
    }
    else if (strstr(requisicao, "GET /set_limits")) {
        // Endpoint para configurar novos limites de alerta via web
        // Extrai parâmetros da URL usando sscanf
        float temp_min, temp_max, umid_min, umid_max, press_min, press_max;
        sscanf(requisicao, "GET /set_limits?temp_min=%f&temp_max=%f&umid_min=%f&umid_max=%f&press_min=%f&press_max=%f",
            &temp_min, &temp_max, &umid_min, &umid_max, &press_min, &press_max);
        // Atualiza variáveis globais com novos limites
        limite_temp_min = temp_min; limite_temp_max = temp_max;
        limite_umid_min = umid_min; limite_umid_max = umid_max;
        limite_press_min = press_min; limite_press_max = press_max;    
        // Resposta simples confirmando alteração
        const char *resposta = "Limites atualizados";
        hs->tamanho = snprintf(hs->resposta, sizeof(hs->resposta),
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
            (int)strlen(resposta), resposta);
    }
    else if (strstr(requisicao, "GET /set_offsets")) {
        // Endpoint para configurar valores de calibração dos sensores
        float offset_temp_aht, offset_temp_bmp, offset_umid, offset_press;
        sscanf(requisicao, "GET /set_offsets?offset_temp_aht=%f&offset_temp_bmp=%f&offset_umid=%f&offset_press=%f",
            &offset_temp_aht, &offset_temp_bmp, &offset_umid, &offset_press);
            
        // Atualiza variáveis de calibração
        ajuste_temp_aht = offset_temp_aht; ajuste_temp_bmp = offset_temp_bmp;
        ajuste_umidade = offset_umid; ajuste_pressao = offset_press;
        
        const char *resposta = "Calibracoes atualizadas";
        hs->tamanho = snprintf(hs->resposta, sizeof(hs->resposta),
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
            (int)strlen(resposta), resposta);
    }
    else if (strstr(requisicao, "GET /graficos")) {
        // Página web com gráficos interativos (HTML + JavaScript)
        hs->tamanho = snprintf(hs->resposta, sizeof(hs->resposta),
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
            (int)strlen(HTML_GRAFICOS), HTML_GRAFICOS);
    }
    else if (strstr(requisicao, "GET /estados")) {
        // Página web mostrando estados do sistema e alertas
        hs->tamanho = snprintf(hs->resposta, sizeof(hs->resposta),
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
            (int)strlen(HTML_ESTADOS), HTML_ESTADOS);
    }
    else if (strstr(requisicao, "GET /limites")) {
        // Página web para configurar limites de alerta
        hs->tamanho = snprintf(hs->resposta, sizeof(hs->resposta),
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
            (int)strlen(HTML_LIMITES), HTML_LIMITES);
    }
    else if (strstr(requisicao, "GET /calibracao")) {
        // Página web para ajustar calibração dos sensores
        hs->tamanho = snprintf(hs->resposta, sizeof(hs->resposta),
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
            (int)strlen(HTML_CALIBRACAO), HTML_CALIBRACAO);
    }
    else {
        // Qualquer outra URL serve a página principal
        hs->tamanho = snprintf(hs->resposta, sizeof(hs->resposta),
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
            (int)strlen(HTML_BODY), HTML_BODY);
    }
    
    // Configura callbacks e envia resposta
    tcp_arg(tpcb, hs);                       // Associa estado da conexão ao PCB
    tcp_sent(tpcb, callback_envio_http);     // Define callback para confirmação de envio
    tcp_write(tpcb, hs->resposta, hs->tamanho, TCP_WRITE_FLAG_COPY); // Envia dados
    tcp_output(tpcb);                        // Força envio imediato
    pbuf_free(p);                            // Libera buffer da requisição recebida
    return ERR_OK;
}

// Callback chamado quando uma nova conexão TCP é estabelecida
// Configura o callback de recepção de dados
static err_t callback_nova_conexao(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, callback_recepcao_http); // Define função para processar dados recebidos
    return ERR_OK;
}

// Inicializa o servidor web na porta 80
// CORREÇÃO: Removido 'static' para corresponder ao protótipo da função.
void iniciar_servidor_web(void) {
    struct tcp_pcb *pcb = tcp_new(); // Cria novo PCB (Protocol Control Block)
    if (!pcb) { 
        printf("Erro ao criar PCB TCP\n"); 
        return; 
    }
    // Associa servidor à porta 80 (HTTP padrão)
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) { 
        printf("Erro ao vincular porta 80\n"); 
        return; 
    }
    pcb = tcp_listen(pcb);                       // Coloca servidor em modo de escuta
    tcp_accept(pcb, callback_nova_conexao);      // Define callback para novas conexões
    printf("Servidor HTTP ativo na porta 80\n");
}

/* =================== FUNÇÕES DE LÓGICA DE ESTADOS =================== */
// Analisa valores atuais dos sensores e determina o estado do sistema
// Esta função implementa a lógica de decisão para alertas
EstadoSistema verificar_estado_atual(void) {
    float pressao_hpa = pressao_atual / 100.0f; // Converte pressão de Pa para hPa
    
    // Verifica condições em ordem de prioridade
    // Temperatura tem prioridade sobre outros parâmetros
    if (temp_media > limite_temp_max) return ESTADO_TEMP_ALTA;
    if (temp_media < limite_temp_min) return ESTADO_TEMP_BAIXA;
    // Depois verifica umidade
    if (umidade_atual > limite_umid_max) return ESTADO_UMID_ALTA;
    if (umidade_atual < limite_umid_min) return ESTADO_UMID_BAIXA;
    // Por último verifica pressão
    if (pressao_hpa > limite_press_max) return ESTADO_PRESS_ALTA;
    if (pressao_hpa < limite_press_min) return ESTADO_PRESS_BAIXA;
    // Se chegou aqui, todos os valores estão dentro dos limites
    return ESTADO_NORMAL;
}

// Converte estado do sistema em descrição textual da cor do LED
// Usado para exibir no display qual cor está ativa
const char* obter_cor_estado_texto(EstadoSistema estado) {
    switch (estado) {
        case ESTADO_NORMAL:       return "Verde";      // Tudo OK = Verde
        case ESTADO_TEMP_ALTA:    return "Vermelho";   // Temperatura alta = Vermelho (quente)
        case ESTADO_TEMP_BAIXA:   return "Azul";       // Temperatura baixa = Azul (frio)
        case ESTADO_UMID_ALTA:    return "Roxo";       // Umidade alta = Roxo (combinação azul+vermelho)
        case ESTADO_UMID_BAIXA:   return "Amarelo";    // Umidade baixa = Amarelo (combinação verde+vermelho)
        case ESTADO_PRESS_ALTA:   return "Branco";     // Pressão alta = Branco (todos LEDs)
        case ESTADO_PRESS_BAIXA:  return "Desligado";  // Pressão baixa = LEDs apagados
        default:                  return "Indefinido";
    }
}

// Converte estado em descrição da animação na matriz de LEDs
// Cada estado tem uma animação visual característica
const char* obter_animacao_estado_texto(EstadoSistema estado) {
    switch (estado) {
        case ESTADO_NORMAL:       return "Quadrado";       // Forma estável
        case ESTADO_TEMP_ALTA:    return "Alerta (!)";     // Símbolo de atenção
        case ESTADO_TEMP_BAIXA:   return "Chuva";          // Animação de gotas caindo
        case ESTADO_UMID_ALTA:    return "Chuva";          // Também usa chuva (muita água)
        case ESTADO_UMID_BAIXA:   return "Alerta (!)";     // Alerta para secura
        case ESTADO_PRESS_ALTA:   return "Quadrado";       // Forma sólida (alta pressão)
        case ESTADO_PRESS_BAIXA:  return "Erro (X)";       // X indica problema
        default:                  return "Nenhuma";
    }
}

/* =================== CONTROLE DE ALERTAS VISUAIS E SONOROS =================== */
// Atualiza os LEDs RGB baseado no estado atual do sistema
// Cada estado tem uma combinação específica de cores
void atualizar_indicadores_led(EstadoSistema estado) {
    // Primeiro apaga todos os LEDs (reset)
    gpio_put(LED_VERDE_PIN, 0); 
    gpio_put(LED_AZUL_PIN, 0); 
    gpio_put(LED_VERMELHO_PIN, 0);
    
    // Acende LEDs conforme o estado
    switch (estado) {
        case ESTADO_NORMAL:       // Verde = tudo normal
            gpio_put(LED_VERDE_PIN, 1); 
            break;
        case ESTADO_TEMP_ALTA:    // Vermelho = calor/perigo
            gpio_put(LED_VERMELHO_PIN, 1); 
            break;
        case ESTADO_TEMP_BAIXA:   // Azul = frio
            gpio_put(LED_AZUL_PIN, 1); 
            break;
        case ESTADO_UMID_ALTA:    // Roxo = azul + vermelho (muita umidade)
            gpio_put(LED_AZUL_PIN, 1); 
            gpio_put(LED_VERMELHO_PIN, 1); 
            break;
        case ESTADO_UMID_BAIXA:   // Amarelo = verde + vermelho (pouca umidade)
            gpio_put(LED_VERDE_PIN, 1); 
            gpio_put(LED_VERMELHO_PIN, 1); 
            break;
        case ESTADO_PRESS_ALTA:   // Branco = todos os LEDs (alta pressão)
            gpio_put(LED_VERDE_PIN, 1); 
            gpio_put(LED_AZUL_PIN, 1); 
            gpio_put(LED_VERMELHO_PIN, 1); 
            break;
        case ESTADO_PRESS_BAIXA:  // LEDs apagados (baixa pressão)
            break; 
    }
}

// Configura frequência do buzzer através do módulo PWM
// Calcula divisores e períodos necessários para gerar a frequência desejada
void definir_frequencia_buzzer(uint slice_num, float freq) {
    uint32_t clock = 125000000;      // Clock do sistema = 125MHz
    uint32_t f_int = (uint32_t)freq;   // Frequência desejada convertida para inteiro
    if (f_int == 0) return;            // Se frequência é zero, não faz nada
    
    // Cálcula divisor necessário para atingir a frequência
    // Formula complexa para trabalhar com limitações do hardware PWM
    uint32_t divisor16 = clock / f_int / 4096 + (clock % (f_int * 4096) != 0);
    if (divisor16 / 16 == 0) divisor16 = 16;     // Valor mínimo
    uint32_t wrap = (uint32_t)(clock * 16.0f / divisor16 / freq) - 1;
    
    // Configura módulo PWM com valores calculados
    pwm_set_clkdiv_int_frac(slice_num, divisor16 / 16, divisor16 & 0xF);
    pwm_set_wrap(slice_num, wrap);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(BUZZER_ALARME_PIN), wrap / 2); // 50% duty cycle
}

// Controla padrões de som do buzzer baseado no estado do sistema
// Valores altos geram sons agudos e rápidos, valores baixos geram sons graves e lentos
void processar_alertas_sonoros(EstadoSistema estado) {
    static EstadoSistema estado_anterior = ESTADO_NORMAL;  // Lembra estado anterior
    static uint64_t proximo_som_ms = 0;                    // Quando tocar próximo som
    static int sequencia_atual = 0;                       // Posição na sequência de sons
    
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_ALARME_PIN); // Identifica slice PWM do buzzer
    
    // Se mudou de estado, reinicia sequência
    if (estado != estado_anterior) { 
        sequencia_atual = 0; 
        proximo_som_ms = 0; 
        pwm_set_enabled(slice_num, false);  // Para o som
        estado_anterior = estado; 
    }
    
    // Se está normal, não toca nada
    if (estado == ESTADO_NORMAL) { 
        pwm_set_enabled(slice_num, false); 
        return; 
    }
    uint64_t agora_ms = to_ms_since_boot(get_absolute_time());
    if (agora_ms < proximo_som_ms) return; // Ainda não é hora de alterar o som
    // Determina se o problema é "valor acima do limite" ou "valor abaixo do limite"
    bool valores_acima = (estado == ESTADO_TEMP_ALTA || estado == ESTADO_UMID_ALTA || estado == ESTADO_PRESS_ALTA);
    
    if (valores_acima) {
        // Som agudo e repetitivo para valores ALTOS (mais urgente)
        // Sequência: BEEP curto - pausa - BEEP - pausa - BEEP - pausa longa
        switch(sequencia_atual) {
            case 0: definir_frequencia_buzzer(slice_num, 2500); pwm_set_enabled(slice_num, true); proximo_som_ms = agora_ms + 100; break; // BEEP
            case 1: case 3: case 5: pwm_set_enabled(slice_num, false); proximo_som_ms = agora_ms + 100; break; // Pausas curtas
            case 2: case 4: pwm_set_enabled(slice_num, true); proximo_som_ms = agora_ms + 100; break; // BEEPs
            default: pwm_set_enabled(slice_num, false); proximo_som_ms = agora_ms + 1500; break; // Pausa longa entre sequências
        }
        sequencia_atual = (sequencia_atual + 1) % 6; // Cicla através da sequência
    } else {
        // Som grave e espaçado para valores BAIXOS (menos urgente)
        // Sequência: BEEP longo grave - pausa longa
        switch(sequencia_atual) {
            case 0: definir_frequencia_buzzer(slice_num, 500); pwm_set_enabled(slice_num, true); proximo_som_ms = agora_ms + 400; break; // BEEP grave longo
            case 1: pwm_set_enabled(slice_num, false); proximo_som_ms = agora_ms + 1000; break; // Pausa longa
        }
        sequencia_atual = (sequencia_atual + 1) % 2; // Alterna entre som e pausa
    }
}

/* =================== FUNÇÃO PRINCIPAL =================== */
int main() {
    // Inicializa sistema de comunicação serial para debug
    stdio_init_all();
    
    // Inicializa matriz de LEDs (função externa)
    inicializar_matriz_led();
    sleep_ms(2000); // Aguarda 2 segundos para estabilizar
    
    // Declara estruturas principais do sistema
    ssd1306_t display;                   // Estrutura para controle do display OLED
    struct bmp280_calib_param params_bmp;    // Parâmetros de calibração do sensor BMP280
    uint64_t proxima_coleta = 0;           // Timestamp da próxima leitura dos sensores
    
    // Inicializa todos os periféricos do sistema
    inicializar_hardware_completo(&display, &params_bmp);
    configurar_botoes_navegacao();   // Configura botões com interrupções
    configurar_joystick_zoom();      // Configura ADC para joystick
    configurar_leds_status();        // Configura LEDs RGB como saída
    configurar_buzzer_alertas();     // Configura PWM para buzzer
    inicializar_conexao_wifi(&display); // Conecta no WiFi e inicia servidor web
    
    // Loop principal infinito
    while (true) {
        // Processa eventos de rede se WiFi estiver conectado
        if (wifi_conectado) {
            cyw43_arch_poll(); // Chama rotinas da pilha TCP/IP
        }
        
        // Verifica se é hora de coletar dados dos sensores (a cada 2 segundos)
        if (time_us_64() >= proxima_coleta) {
            // Lê dados de todos os sensores
            coletar_dados_todos_sensores(&params_bmp, &temp_aht, &temp_bmp, &temp_media, &umidade_atual, &pressao_atual);
            proxima_coleta = time_us_64() + INTERVALO_LEITURA_MS * 1000; // Agenda próxima leitura
            
            // Atualiza buffer circular para gráficos (mantém últimos 30 pontos)
            historico_temp[indice_circular] = temp_media;
            historico_umid[indice_circular] = umidade_atual;
            historico_press[indice_circular] = pressao_atual / 100.0f; // Converte Pa para hPa
            indice_circular = (indice_circular + 1) % TAMANHO_BUFFER_GRAFICO; // Avança índice circular
            if (contador_amostras < TAMANHO_BUFFER_GRAFICO) contador_amostras++; // Conta até encher buffer
            
            // Atualiza buffer maior para interface web (mantém últimos 100 pontos)
            dados_web_temp[indice_web] = temp_media;
            dados_web_umid[indice_web] = umidade_atual;
            dados_web_press[indice_web] = pressao_atual / 100.0f;
            indice_web = (indice_web + 1) % TAMANHO_HISTORICO_WEB; // Avança índice web
            if (contador_web < TAMANHO_HISTORICO_WEB) contador_web++; // Conta até encher buffer web
            
            // Analisa estado atual e atualiza indicadores
            estado_atual = verificar_estado_atual();
            atualizar_indicadores_led(estado_atual);
            // Se está em tela que mostra dados, marca para atualizar display
            if (tela_ativa >= TELA_DADOS_SENSORES) flag_atualizar_display = true;
        }
        
        // Atualiza matriz de LEDs com animação baseada no estado
        atualizar_matriz_pelo_estado(estado_atual);
        // Processa alertas sonoros
        processar_alertas_sonoros(estado_atual);
        
        // Atualiza display se necessário
        if (flag_atualizar_display) {
            atualizar_display_principal(&display, temp_aht, temp_bmp, temp_media, umidade_atual, pressao_atual);
            flag_atualizar_display = false;
        }
        
        // Processa joystick para zoom nos gráficos
        processar_movimento_joystick();
        sleep_ms(10); // Pausa curta para não sobrecarregar CPU
    }
    return 0; // Nunca deveria chegar aqui
}

/* =================== INICIALIZAÇÃO DO HARDWARE =================== */
// Inicializa todos os periféricos necessários
void inicializar_hardware_completo(ssd1306_t *display, struct bmp280_calib_param *params) {
    // Configura barramento I2C para sensores (velocidade 100kHz)
    i2c_init(I2C_SENSORES_PORT, 100 * 1000);
    gpio_set_function(I2C_SENSORES_SDA_PIN, GPIO_FUNC_I2C); // Configura pino como SDA
    gpio_set_function(I2C_SENSORES_SCL_PIN, GPIO_FUNC_I2C); // Configura pino como SCL
    gpio_pull_up(I2C_SENSORES_SDA_PIN);                     // Ativa resistor pull-up interno
    gpio_pull_up(I2C_SENSORES_SCL_PIN);                     // Pull-up necessário para I2C
    // Configura barramento I2C para display (velocidade 400kHz - mais rápido)
    i2c_init(I2C_DISPLAY_PORT, 400 * 1000);
    gpio_set_function(I2C_DISPLAY_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_DISPLAY_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_DISPLAY_SDA_PIN);
    gpio_pull_up(I2C_DISPLAY_SCL_PIN);
    // Inicializa display OLED (SSD1306)
    ssd1306_init(display, LARGURA_DISPLAY, ALTURA_DISPLAY, false, ENDERECO_DISPLAY, I2C_DISPLAY_PORT);
    ssd1306_config(display); // Aplica configurações padrão
    // Inicializa sensores
    aht20_init(I2C_SENSORES_PORT);                 // Inicializa sensor temperatura/umidade
    bmp280_init(I2C_SENSORES_PORT);                // Inicializa sensor pressão/temperatura
    bmp280_get_calib_params(I2C_SENSORES_PORT, params); // Lê parâmetros de calibração do BMP280
}

// Configura botões com interrupções para navegação
void configurar_botoes_navegacao(void) {
    // Configura botão de avançar
    gpio_init(BOTAO_AVANCAR_PIN);                 // Inicializa pino
    gpio_set_dir(BOTAO_AVANCAR_PIN, GPIO_IN);     // Define como entrada
    gpio_pull_up(BOTAO_AVANCAR_PIN);              // Ativa pull-up (botão conecta ao GND)
    // Configura botão de voltar
    gpio_init(BOTAO_VOLTAR_PIN);
    gpio_set_dir(BOTAO_VOLTAR_PIN, GPIO_IN);
    gpio_pull_up(BOTAO_VOLTAR_PIN);
    // Configura interrupções para detectar pressionamento (borda descendente)
    gpio_set_irq_enabled_with_callback(BOTAO_AVANCAR_PIN, GPIO_IRQ_EDGE_FALL, true, &processar_botoes_pressionados);
    gpio_set_irq_enabled_with_callback(BOTAO_VOLTAR_PIN, GPIO_IRQ_EDGE_FALL, true, &processar_botoes_pressionados);
}

// Configura ADC para leitura do joystick analógico
void configurar_joystick_zoom(void) {
    adc_init();                       // Inicializa módulo ADC
    adc_gpio_init(JOYSTICK_ZOOM_PIN); // Configura pino como entrada ADC
    adc_select_input(0);              // Seleciona canal ADC 0 (GPIO 26)
}

// Configura LEDs RGB como saídas digitais
void configurar_leds_status(void) {
    // LED Verde
    gpio_init(LED_VERDE_PIN);
    gpio_set_dir(LED_VERDE_PIN, GPIO_OUT); // Define como saída
    gpio_put(LED_VERDE_PIN, 0);            // Inicia apagado
    // LED Azul  
    gpio_init(LED_AZUL_PIN);
    gpio_set_dir(LED_AZUL_PIN, GPIO_OUT);
    gpio_put(LED_AZUL_PIN, 0);
    // LED Vermelho
    gpio_init(LED_VERMELHO_PIN);
    gpio_set_dir(LED_VERMELHO_PIN, GPIO_OUT);
    gpio_put(LED_VERMELHO_PIN, 0);
}

// Configura PWM para controle do buzzer
void configurar_buzzer_alertas(void) {
    gpio_set_function(BUZZER_ALARME_PIN, GPIO_FUNC_PWM); // Configura pino como saída PWM
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_ALARME_PIN); // Identifica slice PWM
    pwm_set_enabled(slice_num, false);                   // Inicia desabilitado (sem som)
}

// Inicializa conexão WiFi e exibe progresso no display
void inicializar_conexao_wifi(ssd1306_t *display) {
    // Mostra status na tela durante conexão
    ssd1306_fill(display, 0);              // Limpa display
    ssd1306_draw_string(display, "Conectando WiFi", 0, 0, false);
    ssd1306_draw_string(display, "Aguarde...", 0, 30, false);
    ssd1306_send_data(display);            // Envia dados para display
    
    // Inicializa stack WiFi do Pico W
    if (cyw43_arch_init()) {
        // Falhou na inicialização
        ssd1306_fill(display, 0);
        ssd1306_draw_string(display, "WiFi FALHOU", 0, 0, false);
        ssd1306_send_data(display);
        wifi_conectado = false;
        strcpy(endereco_ip, "Erro Init");
        return;
    }
    // Habilita modo estação (cliente WiFi)
    cyw43_arch_enable_sta_mode();
    // Tenta conectar na rede (timeout de 10 segundos)
    if (cyw43_arch_wifi_connect_timeout_ms(NOME_WIFI, SENHA_WIFI, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
        // Falhou na conexão
        ssd1306_fill(display, 0);
        ssd1306_draw_string(display, "WiFi ERRO", 0, 0, false);
        ssd1306_send_data(display);
        wifi_conectado = false;
        strcpy(endereco_ip, "Sem Conexao");
        return;
    }
    
    // Sucesso! Extrai endereço IP obtido via DHCP
    uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
    snprintf(endereco_ip, sizeof(endereco_ip), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    
    // Mostra sucesso na tela
    ssd1306_fill(display, 0);
    ssd1306_draw_string(display, "WiFi CONECTADO", 0, 0, false);
    ssd1306_draw_string(display, endereco_ip, 0, 10, false);
    ssd1306_send_data(display);
    
    wifi_conectado = true;
    iniciar_servidor_web(); // Inicia servidor HTTP
    sleep_ms(2000);         // Deixa mensagem na tela por 2 segundos
}

/* =================== CALLBACKS DE INTERRUPÇÃO =================== */
// Função chamada quando botões são pressionados (via interrupção)
// Implementa debounce por software para evitar múltiplos cliques
void processar_botoes_pressionados(uint gpio, uint32_t eventos) {
    static uint64_t ultimo_botao_a = 0, ultimo_botao_b = 0; // Timestamps dos últimos cliques
    uint64_t agora = time_us_64(); // Tempo atual em microssegundos
    
    // Verifica se foi borda descendente (pressionamento)
    if (eventos & GPIO_IRQ_EDGE_FALL) {
        // Botão A (avançar) - com debounce
        if (gpio == BOTAO_AVANCAR_PIN && agora - ultimo_botao_a > TEMPO_DEBOUNCE_MS * 1000) {
            tela_ativa = (tela_ativa + 1) % TOTAL_TELAS; // Vai para próxima tela (circular)
            flag_atualizar_display = true;               // Marca para redesenhar
            ultimo_botao_a = agora;                      // Atualiza timestamp
        }
        
        // Botão B (voltar) - com debounce  
        if (gpio == BOTAO_VOLTAR_PIN && agora - ultimo_botao_b > TEMPO_DEBOUNCE_MS * 1000) {
            tela_ativa = (tela_ativa == 0) ? (TOTAL_TELAS - 1) : tela_ativa - 1; // Volta uma tela (circular)
            flag_atualizar_display = true;
            ultimo_botao_b = agora;
        }
    }
}

// Processa movimento do joystick para controlar zoom dos gráficos
// Só funciona quando está visualizando gráficos (temperaturas, umidade ou pressão)
void processar_movimento_joystick(void) {
    // Só processa zoom se estiver numa tela de gráfico
    if (tela_ativa < TELA_GRAFICO_TEMP || tela_ativa > TELA_GRAFICO_PRESSAO) return;
    
    const uint16_t ZONA_MORTA_MIN = 1500, ZONA_MORTA_MAX = 2500; // ADC values for dead zone
    uint16_t valor_adc = adc_read();                             // Lê valor do joystick (0-4095)
    uint64_t agora = to_ms_since_boot(get_absolute_time());
    
    // Debounce para evitar alterações muito rápidas
    if (agora - ultimo_zoom_ms < TEMPO_DEBOUNCE_ZOOM_MS) return;
    
    bool mudou_zoom = false;
    
    // Determina qual variável de zoom alterar baseado na tela ativa
    // CORREÇÃO: O ponteiro agora é 'volatile float *' para corresponder ao tipo das variáveis de zoom.
    volatile float *fator_zoom_atual = (tela_ativa == TELA_GRAFICO_TEMP) ? &fator_zoom_temp : 
                                     (tela_ativa == TELA_GRAFICO_UMIDADE) ? &fator_zoom_umid : &fator_zoom_press;
    
    // Verifica direção do joystick e altera zoom
    if (valor_adc > ZONA_MORTA_MAX && *fator_zoom_atual < 4.0f) {
        // Joystick para cima = aumenta zoom (máximo 4x)
        *fator_zoom_atual += 0.10f;
        mudou_zoom = true;
    }
    else if (valor_adc < ZONA_MORTA_MIN && *fator_zoom_atual > 0.25f) {
        // Joystick para baixo = diminui zoom (mínimo 0.25x)
        *fator_zoom_atual -= 0.10f;
        mudou_zoom = true;
    }
    // Se houve mudança, atualiza display
    if (mudou_zoom) {
        ultimo_zoom_ms = agora;
        flag_atualizar_display = true;
    }
}

/* =================== FUNÇÕES DE EXIBIÇÃO =================== */
// Desenha tela inicial/de boas-vindas
void exibir_tela_inicial(ssd1306_t *display) {
    ssd1306_fill(display, 0);                   // Limpa tela (preenche com preto)
    ssd1306_rect(display, 5, 5, 118, 54, 1, false); // Desenha retângulo de borda
    // Centraliza título na tela
    const char *titulo = "PicoAtmos";
    ssd1306_draw_string(display, titulo, (LARGURA_DISPLAY - strlen(titulo) * 8) / 2, 28, false);
    // Centraliza subtítulo
    const char *subtitulo = "Sistema Ativo";
    ssd1306_draw_string(display, subtitulo, (LARGURA_DISPLAY - strlen(subtitulo) * 6) / 2, 45, true);
    
    ssd1306_send_data(display); // Envia dados para o display
}

// Exibe valores atuais de todos os sensores
void exibir_dados_sensores(ssd1306_t *display, float t_aht, float t_bmp, float t_med, float umid, float press) {
    ssd1306_fill(display, 0);
    // Cabeçalho centralizado
    const char *titulo = "Dados Coletados";
    ssd1306_draw_string(display, titulo, (LARGURA_DISPLAY - strlen(titulo) * 8) / 2, 0, false);
    // Buffer para formatação de strings
    char buffer[32];
    // Temperatura do AHT20
    snprintf(buffer, sizeof(buffer), "AHT20: %.1fC", t_aht);
    ssd1306_draw_string(display, buffer, 0, 12, false);
    // Temperatura do BMP280  
    snprintf(buffer, sizeof(buffer), "BMP280:%.1fC", t_bmp);
    ssd1306_draw_string(display, buffer, 0, 22, false);
    // Temperatura média
    snprintf(buffer, sizeof(buffer), "Media: %.1fC", t_med);
    ssd1306_draw_string(display, buffer, 0, 32, false);
    // Umidade
    snprintf(buffer, sizeof(buffer), "Umidade:%.1f%%", umid);
    ssd1306_draw_string(display, buffer, 0, 42, false);
    // Pressão (convertida de Pa para hPa)
    snprintf(buffer, sizeof(buffer), "Press:%.1fhPa", press / 100.0);
    ssd1306_draw_string(display, buffer, 0, 52, false);
    
    ssd1306_send_data(display);
}

// Exibe status da rede e sistema
void exibir_status_conexao(ssd1306_t *display) {
    ssd1306_fill(display, 0);
    const char *titulo = "Status Sistema";
    ssd1306_draw_string(display, titulo, (LARGURA_DISPLAY - strlen(titulo) * 8) / 2, 0, false);
    char buffer[32];
    // Endereço IP atual
    snprintf(buffer, sizeof(buffer), "IP:%s", endereco_ip);
    ssd1306_draw_string(display, buffer, 0, 12, false);
    // Status WiFi
    snprintf(buffer, sizeof(buffer), "WiFi:%s", wifi_conectado ? "OK" : "FALHA");
    ssd1306_draw_string(display, buffer, 0, 22, false);
    // Analisa e exibe status de cada parâmetro
    const char *status_temp = (temp_media < limite_temp_min) ? "Baixa" : 
                              (temp_media > limite_temp_max) ? "Alta" : "OK";
    snprintf(buffer, sizeof(buffer), "Temp:%s", status_temp);
    ssd1306_draw_string(display, buffer, 0, 32, false);
    const char *status_umid = (umidade_atual < limite_umid_min) ? "Baixa" : 
                              (umidade_atual > limite_umid_max) ? "Alta" : "OK";
    snprintf(buffer, sizeof(buffer), "Umid:%s", status_umid);
    ssd1306_draw_string(display, buffer, 0, 42, false);
    float pressao_hpa = pressao_atual / 100.0f;
    const char *status_press = (pressao_hpa < limite_press_min) ? "Baixa" : 
                               (pressao_hpa > limite_press_max) ? "Alta" : "OK";
    snprintf(buffer, sizeof(buffer), "Press:%s", status_press);
    ssd1306_draw_string(display, buffer, 0, 52, false);
    ssd1306_send_data(display);
}

// Exibe status atual do LED RGB
void exibir_status_led_rgb(ssd1306_t *display) {
    ssd1306_fill(display, 0);
    const char *titulo = "Indicador LED";
    ssd1306_draw_string(display, titulo, (LARGURA_DISPLAY - strlen(titulo) * 8) / 2, 0, false);
    ssd1306_hline(display, 0, LARGURA_DISPLAY, 12, true); // Linha separadora
    // Obtém descrição da cor atual
    const char *cor_ativa = obter_cor_estado_texto(estado_atual);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "Cor Ativa:");
    ssd1306_draw_string(display, buffer, 10, 25, false);
    // Centraliza nome da cor
    ssd1306_draw_string(display, cor_ativa, (LARGURA_DISPLAY - strlen(cor_ativa) * 8) / 2, 40, false);
    ssd1306_send_data(display);
}

// Função genérica para desenhar qualquer gráfico com zoom
// Recebe array de dados, fator de zoom e unidade de medida
void desenhar_grafico_base(ssd1306_t *display, const char *titulo, float *buffer_dados, float fator_zoom, const char *unidade) {
    ssd1306_fill(display, 0);
    // Define área do gráfico na tela
    const uint8_t area_x = 20, area_y = 52, altura = 40, largura = 105;
    // Título centralizado
    ssd1306_draw_string(display, titulo, (LARGURA_DISPLAY - strlen(titulo) * 8) / 2, 0, false);
    // Se não há dados ainda, mostra mensagem
    if (contador_amostras == 0) {
        ssd1306_draw_string(display, "Sem dados...", 8, 30, false);
        ssd1306_send_data(display);
        return;
    }
    
    // Encontra valores mínimo e máximo no buffer para escalar gráfico
    float val_min = buffer_dados[0], val_max = val_min;
    for (int i = 0; i < contador_amostras; ++i) {
        int idx = (indice_circular - contador_amostras + i + TAMANHO_BUFFER_GRAFICO) % TAMANHO_BUFFER_GRAFICO;
        float val = buffer_dados[idx];
        if (val < val_min) val_min = val;
        if (val > val_max) val_max = val;
    }
    
    // Garante faixa mínima para evitar divisão por zero
    if (val_max - val_min < 2.0f) {
        float media = (val_max + val_min) / 2.0f;
        val_min = media - 1.0f;
        val_max = media + 1.0f;
    }
    
    // Aplica zoom centralizando na média dos valores
    float faixa_zoom = (val_max - val_min) / fator_zoom;
    float centro = (val_max + val_min) * 0.5f;
    float y_min = centro - faixa_zoom * 0.5f;
    float y_max = centro + faixa_zoom * 0.5f;
    
    // Desenha eixos do gráfico
    ssd1306_hline(display, area_x, area_x + largura, area_y, true);     // Eixo X
    ssd1306_vline(display, area_x, area_y - altura, area_y, true);      // Eixo Y
    
    // Marcações no eixo Y (3 divisões)
    float passo = faixa_zoom / 3.0f;
    if (passo < 0.1f) passo = 0.1f; // Passo mínimo
    
    for (int i = 0; i <= 3; i++) {
        float valor_marca = y_min + (i * faixa_zoom / 3.0f);
        uint8_t y_pos = area_y - (i * altura / 3);
        // Desenha tick mark
        ssd1306_hline(display, area_x - 2, area_x, y_pos, true);
        // Label numérico
        char marca[8];
        snprintf(marca, sizeof(marca), "%.0f", valor_marca);
        ssd1306_draw_string(display, marca, 0, y_pos - 4, false);
    }
    // Marcações no eixo X (tempo: 0, 30s, 60s)
    ssd1306_vline(display, area_x, area_y, area_y + 2, true);
    ssd1306_vline(display, area_x + largura/2, area_y, area_y + 2, true);
    ssd1306_vline(display, area_x + largura, area_y, area_y + 2, true);
    ssd1306_draw_string(display, "0", area_x - 3, area_y + 5, false);
    ssd1306_draw_string(display, "30s", area_x + largura/2 - 10, area_y + 5, false);
    ssd1306_draw_string(display, "60s", area_x + largura - 18, area_y + 5, false);
    
    // Desenha linhas conectando pontos do gráfico
    if (contador_amostras > 1) {
        for (int i = 0; i < contador_amostras - 1; ++i) {
            // Índices dos pontos atual e próximo no buffer circular
            int idx1 = (indice_circular - contador_amostras + i + TAMANHO_BUFFER_GRAFICO) % TAMANHO_BUFFER_GRAFICO;
            int idx2 = (idx1 + 1) % TAMANHO_BUFFER_GRAFICO;
            
            float val1 = buffer_dados[idx1], val2 = buffer_dados[idx2];
            
            // Converte valores para coordenadas de pixel
            uint8_t x1 = area_x + (i * largura) / (TAMANHO_BUFFER_GRAFICO - 1);
            uint8_t y1 = area_y - (uint8_t)(((val1 - y_min) / faixa_zoom) * altura);
            uint8_t x2 = area_x + ((i + 1) * largura) / (TAMANHO_BUFFER_GRAFICO - 1);
            uint8_t y2 = area_y - (uint8_t)(((val2 - y_min) / faixa_zoom) * altura);
            
            // Desenha linha entre os pontos
            ssd1306_line(display, x1, y1, x2, y2, true);
        }
    }
    
    ssd1306_send_data(display);
}

// Funções específicas para cada tipo de gráfico
void exibir_grafico_temperatura(ssd1306_t *display) {
    desenhar_grafico_base(display, "Temperatura", historico_temp, fator_zoom_temp, "C");
}
void exibir_grafico_umidade(ssd1306_t *display) {
    desenhar_grafico_base(display, "Umidade", historico_umid, fator_zoom_umid, "%");
}
void exibir_grafico_pressao(ssd1306_t *display) {
    desenhar_grafico_base(display, "Pressao", historico_press, fator_zoom_press, "hPa");
}

/* =================== CONTROLE PRINCIPAL DO DISPLAY =================== */
// Função principal que decide qual tela desenhar baseada na variável tela_ativa
void atualizar_display_principal(ssd1306_t *display, float t_aht, float t_bmp, float t_med, float umid, float press) {
    switch (tela_ativa) {
        case TELA_INICIAL:          exibir_tela_inicial(display); break;
        case TELA_DADOS_SENSORES:   exibir_dados_sensores(display, t_aht, t_bmp, t_med, umid, press); break;
        case TELA_STATUS_REDE:      exibir_status_conexao(display); break;
        case TELA_GRAFICO_TEMP:     exibir_grafico_temperatura(display); break;
        case TELA_GRAFICO_UMIDADE:  exibir_grafico_umidade(display); break;
        case TELA_GRAFICO_PRESSAO:  exibir_grafico_pressao(display); break;
        case TELA_STATUS_LEDS:      exibir_status_led_rgb(display); break;
    }
}

/* =================== COLETA DE DADOS DOS SENSORES =================== */
// Lê dados de todos os sensores e aplica calibrações
void coletar_dados_todos_sensores(struct bmp280_calib_param *params, float *t_aht, float *t_bmp, float *t_med, float *umid, float *press) {
    // Lê sensor AHT20 (temperatura + umidade)
    AHT20_Data dados_aht;
    aht20_read(I2C_SENSORES_PORT, &dados_aht);
    *t_aht = dados_aht.temperature + ajuste_temp_aht; // Aplica calibração
    *umid = dados_aht.humidity + ajuste_umidade;      // Aplica calibração
    
    // Lê sensor BMP280 (temperatura + pressão)
    int32_t temp_raw, press_raw;
    bmp280_read_raw(I2C_SENSORES_PORT, &temp_raw, &press_raw); // Lê valores brutos
    
    // Converte valores brutos usando parâmetros de calibração do sensor
    int32_t temp_conv = bmp280_convert_temp(temp_raw, params);
    int32_t press_conv = bmp280_convert_pressure(press_raw, temp_raw, params);
    
    // Converte para float e aplica calibrações
    *t_bmp = (temp_conv / 100.0f) + ajuste_temp_bmp;       // BMP280 retorna temp * 100
    *press = press_conv + (ajuste_pressao * 100.0f);      // Pressão em Pa, ajuste em hPa
    
    // Calcula temperatura média dos dois sensores
    *t_med = (*t_aht + *t_bmp) / 2.0f;
}