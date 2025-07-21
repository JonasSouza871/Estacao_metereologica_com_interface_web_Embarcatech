#include "bmp280.h"
#include "hardware/i2c.h"

/* ---------- Configurações do Sensor BMP280 ---------- */
#define ADDR _u(0x77)

/* ---------- Funções Públicas ---------- */

void bmp280_init(i2c_inst_t *i2c) {
    uint8_t buf[2];
    
    // Configura registro de configuração
    const uint8_t reg_config_val = ((0x04 << 5) | (0x05 << 2)) & 0xFC;
    buf[0] = REG_CONFIG;
    buf[1] = reg_config_val;
    i2c_write_blocking(i2c, ADDR, buf, 2, false);

    // Configura registro de controle de medição
    const uint8_t reg_ctrl_meas_val = (0x01 << 5) | (0x03 << 2) | (0x03);
    buf[0] = REG_CTRL_MEAS;
    buf[1] = reg_ctrl_meas_val;
    i2c_write_blocking(i2c, ADDR, buf, 2, false);
}

void bmp280_read_raw(i2c_inst_t *i2c, int32_t* temp, int32_t* pressure) {
    uint8_t buf[6];
    uint8_t reg = REG_PRESSURE_MSB;
    
    i2c_write_blocking(i2c, ADDR, &reg, 1, true);
    i2c_read_blocking(i2c, ADDR, buf, 6, false);

    *pressure = (buf[0] << 12) | (buf[1] << 4) | (buf[2] >> 4);
    *temp = (buf[3] << 12) | (buf[4] << 4) | (buf[5] >> 4);
}

void bmp280_reset(i2c_inst_t *i2c) {
    uint8_t buf[2] = { REG_RESET, 0xB6 };
    i2c_write_blocking(i2c, ADDR, buf, 2, false);
}

void bmp280_get_calib_params(i2c_inst_t *i2c, struct bmp280_calib_param* params) {
    uint8_t buf[NUM_CALIB_PARAMS] = { 0 };
    uint8_t reg = REG_DIG_T1_LSB;
    
    i2c_write_blocking(i2c, ADDR, &reg, 1, true);
    i2c_read_blocking(i2c, ADDR, buf, NUM_CALIB_PARAMS, false);

    // Parâmetros de calibração de temperatura
    params->dig_t1 = (uint16_t)(buf[1] << 8) | buf[0];
    params->dig_t2 = (int16_t)(buf[3] << 8) | buf[2];
    params->dig_t3 = (int16_t)(buf[5] << 8) | buf[4];

    // Parâmetros de calibração de pressão
    params->dig_p1 = (uint16_t)(buf[7] << 8) | buf[6];
    params->dig_p2 = (int16_t)(buf[9] << 8) | buf[8];
    params->dig_p3 = (int16_t)(buf[11] << 8) | buf[10];
    params->dig_p4 = (int16_t)(buf[13] << 8) | buf[12];
    params->dig_p5 = (int16_t)(buf[15] << 8) | buf[14];
    params->dig_p6 = (int16_t)(buf[17] << 8) | buf[16];
    params->dig_p7 = (int16_t)(buf[19] << 8) | buf[18];
    params->dig_p8 = (int16_t)(buf[21] << 8) | buf[20];
    params->dig_p9 = (int16_t)(buf[23] << 8) | buf[22];
}

/* ---------- Funções de Conversão ---------- */

// Função intermediária que calcula temperatura de resolução fina
// Usada tanto para conversões de pressão quanto de temperatura
int32_t bmp280_convert(int32_t temp, struct bmp280_calib_param* params) {
    // Usa compensação de ponto fixo de 32 bits implementada no datasheet
    int32_t var1, var2;
    
    var1 = ((((temp >> 3) - ((int32_t)params->dig_t1 << 1))) * 
            ((int32_t)params->dig_t2)) >> 11;
    
    var2 = (((((temp >> 4) - ((int32_t)params->dig_t1)) * 
             ((temp >> 4) - ((int32_t)params->dig_t1))) >> 12) * 
            ((int32_t)params->dig_t3)) >> 14;
    
    return var1 + var2;
}

int32_t bmp280_convert_temp(int32_t temp, struct bmp280_calib_param* params) {
    // Utiliza parâmetros de calibração do BMP280 para compensar valor de temperatura
    int32_t t_fine = bmp280_convert(temp, params);
    return (t_fine * 5 + 128) >> 8;
}

int32_t bmp280_convert_pressure(int32_t pressure, int32_t temp, struct bmp280_calib_param* params) {
    // Utiliza parâmetros de calibração do BMP280 para compensar valor de pressão
    int32_t t_fine = bmp280_convert(temp, params);
    int32_t var1, var2;
    uint32_t converted = 0.0;
    
    var1 = (((int32_t)t_fine) >> 1) - (int32_t)64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * ((int32_t)params->dig_p6);
    var2 += ((var1 * ((int32_t)params->dig_p5)) << 1);
    var2 = (var2 >> 2) + (((int32_t)params->dig_p4) << 16);
    
    var1 = (((params->dig_p3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) + 
            ((((int32_t)params->dig_p2) * var1) >> 1)) >> 18;
    var1 = ((((32768 + var1)) * ((int32_t)params->dig_p1)) >> 15);
    
    if (var1 == 0) {
        return 0;  // Evita exceção causada por divisão por zero
    }
    
    converted = (((uint32_t)(((int32_t)1048576) - pressure) - (var2 >> 12))) * 3125;
    
    if (converted < 0x80000000) {
        converted = (converted << 1) / ((uint32_t)var1);
    } else {
        converted = (converted / (uint32_t)var1) * 2;
    }
    
    var1 = (((int32_t)params->dig_p9) * 
            ((int32_t)(((converted >> 3) * (converted >> 3)) >> 13))) >> 12;
    var2 = (((int32_t)(converted >> 2)) * ((int32_t)params->dig_p8)) >> 13;
    converted = (uint32_t)((int32_t)converted + ((var1 + var2 + params->dig_p7) >> 4));
    
    return converted;
}
