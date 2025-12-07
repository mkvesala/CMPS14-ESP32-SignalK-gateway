#pragma once

#include <Arduino.h>
#include <Wire.h>

class CMPS14Sensor {
public:

    explicit CMPS14Sensor(uint8_t i2c_addr);

    bool begin(TwoWire &wirePort);
    bool available() const;
    bool read(float &angle_deg, float &pitch_deg, float &roll_deg);
    bool sendCommand(uint8_t cmd);
    uint8_t readRegister(uint8_t reg);
    bool isAck(uint8_t byte);
    bool isNack(uint8_t byte);

private:
    
    uint8_t addr;
    TwoWire *wire;

    // CMPS14 register map
    static constexpr uint8_t REG_ANGLE_16_H    = 0x02;  // 16-bit angle * 10 (hi)
    static constexpr uint8_t REG_ANGLE_16_L    = 0x03;  // 16-bit angle * 10 (lo)
    static constexpr uint8_t REG_PITCH         = 0x04;  // signed degrees
    static constexpr uint8_t REG_ROLL          = 0x05;  // signed degrees
    static constexpr uint8_t REG_ACK1          = 0x55;  // Ack (new firmware)
    static constexpr uint8_t REG_ACK2          = 0x07;  // Ack (CMPS12 compliant)
    static constexpr uint8_t REG_NACK          = 0xFF;  // Nack
    static constexpr uint8_t REG_CMD           = 0x00;  // Command byte, write before sending other commands

};
