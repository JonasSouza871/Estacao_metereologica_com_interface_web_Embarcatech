#ifndef BMP280_H
#define BMP280_H

#include "hardware/i2c.h"
#include <stdint.h>

/* ---------- Configurações do Sensor BMP280 ---------- */
#define ADDR _u(0x77)
#define NUM_CALIB_PARAMS 24

/* ---------- Registros de Controle ---------- */
#define REG_CONFIG _u(0xF5)
#define REG_CTRL_MEAS _u(0xF4)
#define REG_RESET _u(0xE0)

/* ---------- Registros de Dados de Temperatura ---------- */
#define REG_TEMP_MSB _u(0xFA)
#define REG_TEMP_LSB _u(0xFB)
#define REG_TEMP_XLSB _u(0xFC)

/* ---------- Registros de Dados de Pressão ---------- */
#define REG_PRESSURE_MSB _u(0xF7)
#define REG_PRESSURE_LSB _u(0xF8)
#define REG_PRESSURE_XLSB _u(0xF9)

/* ---------- Registros de Calibração - Temperatura ---------- */
#define REG_DIG_T1_LSB _u(0x88)
#define REG_DIG_T1_MSB _u(0x89)
#define REG_DIG_T2_LSB _u(0x8A)
#define REG_DIG_T2_MSB _u(0x8B)
#define REG_DIG_T3_LSB _u(0x8C)
#define REG_DIG_T3_MSB _u(0x8D)

/* ---------- Registros de Calibração - Pressão ---------- */
#define REG_DIG_P1_LSB _u(0x8E)
#define REG_DIG_P1_MSB _u(0x8F)
#define REG_DIG_P2_LSB _u(0x90)
#define REG_DIG_P2_MSB _u(0x91)
#define REG_DIG_P3_LSB _u(0x92)
#define REG_DIG_P3_MSB _u(0x93)
#define REG_DIG_P4_LSB _u(0x94)
#define REG_DIG_P4_MSB _u(0x95)
#define REG_DIG_P5_LSB _u(0x96)
#define REG_DIG_P5_MSB _u(0x97)
#define REG_DIG_P6_LSB _u(0x98)
#define REG_DIG_P6_MSB _u(0x99)
#define REG_DIG_P7_LSB _u(0x9A)
#define REG_DIG_P7_MSB _u(0x9B)
#define REG_DIG_P8_LSB _u(0x9C)
#define REG_DIG_P8_MSB _u(0x9D)
#define REG_DIG_P9_LSB _u(0x9E)
#define REG_DIG_P9_MSB _u(0x9F)

/* ---------- Estrutura de Parâmetros de Calibração ---------- */
struct bmp280_calib_param {
    // Parâmetros de calibração de temperatura
    uint16_t dig_t1;
    int16_t dig_t2;
    int16_t dig_t3;

    // Parâmetros de calibração de pressão
    uint16_t dig_p1;
    int16_t dig_p2;
    int16_t dig_p3;
    int16_t dig_p4;
    int16_t dig_p5;
    int16_t dig_p6;
    int16_t dig_p7;
    int16_t dig_p8;
    int16_t dig_p9;
};

/* ---------- API do Sensor BMP280 ---------- */

// Inicializa o sensor BMP280
void bmp280_init(i2c_inst_t *i2c);

// Lê dados brutos de temperatura e pressão
void bmp280_read_raw(i2c_inst_t *i2c, int32_t* temp, int32_t* pressure);

// Reseta o sensor BMP280
void bmp280_reset(i2c_inst_t *i2c);

// Obtém parâmetros de calibração do sensor
void bmp280_get_calib_params(i2c_inst_t *i2c, struct bmp280_calib_param* params);

// Converte temperatura bruta para valor calibrado
int32_t bmp280_convert_temp(int32_t temp, struct bmp280_calib_param* params);

// Converte pressão bruta para valor calibrado
int32_t bmp280_convert_pressure(int32_t pressure, int32_t temp, struct bmp280_calib_param* params);

#endif
