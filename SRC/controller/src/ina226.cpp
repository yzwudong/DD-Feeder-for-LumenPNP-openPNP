#include "ina226.h"
#include <Wire.h>

/* 硬件接线（见 docs/12V总线电源设计.md）:
 *   SDA=GPIO22  SCL=GPIO23  上拉 5.1k 到 3.3V   A0/A1 接地 → 地址 0x40 */
#define INA_SDA_PIN   22
#define INA_SCL_PIN   23
#define INA_ADDR      0x40

#define REG_CONFIG    0x00
#define REG_SHUNT     0x01
#define REG_BUS       0x02
#define REG_CURRENT   0x04
#define REG_CAL       0x05

/* 10mΩ 分流电阻 + CAL=5120 → Current_LSB = 100µA (0.1mA/bit)
 * 校验: CAL = 0.00512 / (Current_LSB × Rshunt) = 0.00512/(0.0001×0.01) = 5120 */
#define INA_CAL_VAL   5120
#define INA_CURR_LSB  0.0001f
#define INA_BUS_LSB   0.00125f     /* 1.25 mV/bit */

static bool s_present = false;

static bool wr16(uint8_t reg, uint16_t v) {
    Wire.beginTransmission(INA_ADDR);
    Wire.write(reg);
    Wire.write((uint8_t)(v >> 8));
    Wire.write((uint8_t)(v & 0xFF));
    return Wire.endTransmission() == 0;
}

static bool rd16(uint8_t reg, uint16_t* out) {
    Wire.beginTransmission(INA_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)INA_ADDR, (uint8_t)2) != 2) return false;
    uint16_t hi = (uint16_t)Wire.read();
    uint16_t lo = (uint16_t)Wire.read();
    *out = (hi << 8) | lo;
    return true;
}

void inaInit() {
    Wire.begin(INA_SDA_PIN, INA_SCL_PIN);
    Wire.setClock(400000);

    uint16_t probe = 0;
    if (!rd16(REG_CAL, &probe)) {
        s_present = false;      /* 老板没有 INA226, 静默降级 */
        return;
    }
    /* AVG=4, VBUS/VSH 1.1ms, 连续测量 → 0x4527 */
    wr16(REG_CONFIG, 0x4527);
    wr16(REG_CAL, INA_CAL_VAL);
    s_present = true;
}

bool inaPresent() { return s_present; }

bool inaRead(float* volt, float* amp) {
    if (!s_present) return false;
    uint16_t b = 0, c = 0;
    if (!rd16(REG_BUS, &b) || !rd16(REG_CURRENT, &c)) {
        s_present = false;      /* 掉线后不再重试, 避免拖慢主循环 */
        return false;
    }
    if (volt) *volt = (float)(b & 0x7FFF) * INA_BUS_LSB;
    if (amp)  *amp  = (float)(int16_t)c * INA_CURR_LSB;
    return true;
}
