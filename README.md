# 🚀 PicoAtmos – Estação de Monitoramento Ambiental Inteligente 🚀

Uma estação meteorológica completa, baseada no Raspberry Pi Pico W, com interface web para monitoramento, configuração e alertas em tempo real.

---

### 📝 Descrição Breve

O **PicoAtmos** é um protótipo funcional de uma estação de monitoramento ambiental que transforma o Raspberry Pi Pico W em um hub de inteligência ambiental. O sistema coleta dados de temperatura, umidade e pressão, exibindo-os em um display OLED local e, como principal diferencial, em uma plataforma web completa. Esta interface permite não apenas a visualização de gráficos dinâmicos, mas também a configuração remota de limites de alerta e parâmetros de calibração dos sensores, tornando-o uma ferramenta de monitoramento altamente flexível e adaptável.

---

### ✨ Funcionalidades Principais

-   **📊 Coleta e Visualização de Dados:** Medição contínua de temperatura (dois sensores), umidade e pressão, com exibição local em display OLED e remota via web.
-   **🌐 Servidor Web Embarcado:** Interface web completa, responsiva e acessível por qualquer navegador na mesma rede, para monitoramento e controle total.
-   **📈 Gráficos em Tempo Real:** Visualização de dados históricos em gráficos dinâmicos (via `Chart.js` e `AJAX`) tanto na interface web quanto no display OLED.
-   **🔔 Sistema de Alertas Multimodais:** Alertas sonoros (buzzer) e visuais (LED RGB e Matriz de LED 8x8) ativados quando os limites operacionais são violados, com feedback específico para cada tipo de anomalia.
-   **🔧 Configuração Remota:** Capacidade de ajustar remotamente os limites de alerta (mínimo/máximo) e aplicar offsets de calibração para cada sensor através da interface web.
-   **🖱 Interação Local Avançada:** Navegação entre telas no display OLED através de botões físicos e controle de zoom nos gráficos locais via joystick analógico.

---

### 🖼 Galeria do Projeto

| **Interface Web Principal** | **Gráficos Dinâmicos** | **Configuração de Limites** |
|:---:|:---:|:---:|
| ![Interface Web](https://github.com/user-attachments/assets/ffd083de-75ab-4b10-b740-830357368cfd) | ![Gráfico Tempo Real](https://github.com/user-attachments/assets/66a70c96-98fb-40df-bf17-2b4b2bc56f82) | ![Configuração de Limites](https://github.com/user-attachments/assets/56d78623-a56d-4312-ac86-8205355f8f98) |
| Dashboard centralizado para uma visão geral do sistema. | Análise de tendências com gráficos que se atualizam automaticamente. | Ajuste fino dos parâmetros de alerta de qualquer lugar da sua rede. |

| **Display Local em Ação** | **Alertas Visuais e Sonoros** |
|:---:|:---:|
| ![Display OLED](https://github.com/user-attachments/assets/4a62e978-b75c-42f9-a89f-e432938b8e1a) | ![Alertas](https://github.com/user-attachments/assets/279c8205-5cdd-4e75-8c50-712e9a741737) |
| Feedback instantâneo no display OLED e LED de status. | Sistema rico de feedback com ícones, cores e sons específicos. |

---


### ⚙ Hardware Necessário

| Componente | Quant. | Observações |
| :--- | :---: | :--- |
| Raspberry Pi Pico W | 1 | O cérebro do projeto, com conectividade Wi-Fi. |
| Sensor AHT20 | 1 | Medição de Temperatura e Umidade (I2C). |
| Sensor BMP280 | 1 | Medição de Temperatura e Pressão (I2C). |
| Display OLED 128x64 | 1 | Para feedback visual local (I2C). |
| Matriz de LED 5x5 | 1 | Para alertas visuais com ícones e animações. |
| LED RGB (Catodo Comum) | 1 | Para indicação de status por cores. |
| Buzzer Passivo | 1 | Para alertas sonoros. |
| Botões Momentâneos | 2 | Para navegação local (Avançar/Voltar). |
| Joystick Analógico (1 eixo) | 1 | Para controle de zoom nos gráficos locais. |
| Protoboard e Jumpers | - | Para montagem do circuito. |
| Fonte de Alimentação | 1 | Via USB (5V, ≥1A). |

---

### 🔌 Conexões e Configuração

#### Pinagem Resumida

O projeto utiliza dois barramentos I2C para evitar conflitos e otimizar a comunicação.

**Barramento I2C 0 (Sensores):**
-   `AHT20/BMP280 SDA` -> `GPIO 0`
-   `AHT20/BMP280 SCL` -> `GPIO 1`

**Barramento I2C 1 (Display):**
-   `OLED SDA` -> `GPIO 14`
-   `OLED SCL` -> `GPIO 15`

**Controles e Alertas:**
-   `Botão Avançar` -> `GPIO 5`
-   `Botão Voltar` -> `GPIO 6`
-   `Matriz de LED (DIN)` -> `GPIO 7`
-   `Buzzer` -> `GPIO 10 (PWM)`
-   `LED Verde` -> `GPIO 11`
-   `LED Azul` -> `GPIO 12`
-   `LED Vermelho` -> `GPIO 13`
-   `Joystick (Eixo Y)` -> `GPIO 26 (ADC0)`

> **⚠ Importante:** Garanta um `GND` comum entre todos os componentes. A maioria dos módulos opera em 3.3V, fornecidos pelo pino `3V3(OUT)` do Pico.

---

### 🚀 Começando

#### Pré-requisitos de Software

-   **SDK:** Raspberry Pi Pico SDK
-   **Linguagem:** C/C++
-   **IDE Recomendada:** VS Code com a extensão "CMake Tools"
-   **Toolchain:** ARM GNU Toolchain
-   **Build System:** CMake
-   **Sistema Operacional Testado:** Windows 11, Ubuntu 22.04

#### Configuração e Compilação

Siga estes passos para configurar, compilar e carregar o firmware no seu Pico W.

```bash
# 1. Clone o repositório do projeto
https://github.com/JonasSouza871/Estacao_metereologica_com_interface_web_Embarcatech.git
cd Estacao_metereologica_com_interface_web_Embarcatech

# 2. Configure as credenciais do Wi-Fi
# Edite o arquivo 'main.c' e insira o SSID e a senha 
# da sua rede nas macros no início do arquivo:
# define NOME_WIFI "SUA_REDE_WIFI"
# define SENHA_WIFI "SUA_SENHA"

# 3. Configure o ambiente de build com CMake
# (Certifique-se de que o PICO_SDK_PATH está definido como variável de ambiente)
mkdir build
cd build
cmake ..

# 4. Compile o projeto (use -j para acelerar)
make -j$(nproc)

# 5. Carregue o firmware
# Pressione e segure o botão BOOTSEL no Pico W enquanto o conecta ao USB.
# O Pico será montado como um drive USB (RPI-RP2).
# Copie o arquivo .uf2 gerado para o drive:
cp PicoAtmos.uf2 /media/user/RPI-RP2
```

#### Execução e Monitoramento

-   **Monitor Serial:** Após o upload, abra um monitor serial (Ex: `minicom`, `putty` ou o monitor do VS Code) com **Baud Rate: 115200**. Você verá os logs de inicialização, incluindo o endereço IP do dispositivo após a conexão com o Wi-Fi.
-   **Interface Web:** Abra um navegador e acesse o endereço IP exibido no monitor serial (Ex: `http://192.168.1.10`).
-   **Interface Local:** O display OLED será ativado, e você poderá navegar pelas telas usando os botões.

---

### 📁 Estrutura do Projeto

```
.
├── lib/
│   ├── Display_Bibliotecas/
│   ├── Matriz_Bibliotecas/
│   ├── Wifi_Bibliotecas/
│   ├── aht20.c
│   ├── aht20.h
│   ├── bmp280.c
│   └── bmp280.h
│   └── html.c
│   └── html.h
├── main.c
├── CMakeLists.txt
└── README.md

### 🐛 Solução de Problemas

-   **Dispositivo não conecta ao Wi-Fi?**
    -   Verifique se o SSID e a senha no `main.c` estão corretos.
    -   Certifique-se de que sua rede Wi-Fi é de 2.4 GHz.
    -   Verifique a intensidade do sinal.
-   **Componente I²C (Sensor/Display) não funciona?**
    -   Confirme a pinagem (SDA/SCL) e a soldagem.
    -   Verifique se não há conflitos de endereço I²C. Este projeto usa dois barramentos justamente para evitar isso.
-   **Comportamento instável ou reinicializações?**
    -   Pode ser um problema de alimentação. Certifique-se de que a fonte de energia (via USB) é estável e fornece corrente suficiente.
-   **Falha na compilação?**
    -   Verifique se o `PICO_SDK_PATH` está corretamente definido em suas variáveis de ambiente.
    -   Certifique-se de que todas as subdependências do SDK foram instaladas corretamente.

---

### 👤 Autor

**Jonas Souza**

-   **E-mail:** `Jonassouza871@hotmail.com`

