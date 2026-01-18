#pragma once

#include <Arduino.h>
#include <Wire.h>

// === C M P S 1 4 S E N S O R  C L A S S ===
//
// - Class CMPS14Sensor - "the sensor" responsible for the actual CMPS14 device
// - Initialise: if (sensor.begin(Wire)) ...
// - Read raw data to float variables:
//      float angle_deg, pitch_deg, roll_deg;
//      if (sensor.available() && sensor.read(angle_deg, pitch_deg, roll_deg)) ...
// - Send a command byte:
//      uint8_t cmd = 0x80;
//      if (sensor.sendCommand(cmd)) ...
// - Read a register value:
//      uint8_t reg = 0x04;
//      uint8_t ack = sensor.readRegister(reg);
//      if (sensor.isAck(ack)) ...
// - Uses: TwoWire

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
