#pragma once

#include <Arduino.h>
#include <Wire.h>

class CMPS14Sensor {
public:

    explicit CMPS14Sensor(uint8_t i2c_addr = CMPS14_ADDR);

    bool begin(TwoWire &wirePort);
    bool available() const;
    bool read(float &angle_deg, float &pitch_deg, float &roll_deg);
    bool sendCommand(uint8_t cmd);
    uint8_t readRegister(uint8_t reg);

private:
    
    uint8_t addr;
    TwoWire *wire;

    // CMPS14 register map
    static const uint8_t REG_ANGLE_16_H    = 0x02;  // 16-bit angle * 10 (hi)
    static const uint8_t REG_ANGLE_16_L    = 0x03;  // 16-bit angle * 10 (lo)
    static const uint8_t REG_PITCH         = 0x04;  // signed degrees
    static const uint8_t REG_ROLL          = 0x05;  // signed degrees
    static const uint8_t REG_ACK1          = 0x55;  // Ack (new firmware)
    static const uint8_t REG_ACK2          = 0x07;  // Ack (CMPS12 compliant)
    static const uint8_t REG_NACK          = 0xFF;  // Nack
    static const uint8_t REG_CMD           = 0x00;  // Command byte, write before sending other commands
    static const uint8_t CMPS14_ADDR       = 0x60;  // I2C address of CMPS14

};
