#include "CMPS14Sensor.h"

// Constructor
CMPS14Sensor::CMPS14Sensor(uint8_t i2c_addr) : addr(i2c_addr), wire(&Wire) {}

// Begin sensor
bool CMPS14Sensor::begin(TwoWire &wirePort) {
    wire = &wirePort;
    wire->beginTransmission(addr);
    return (wire->endTransmission() == 0);
}

// Check availability of sensor
bool CMPS14Sensor::available() const {
    wire->beginTransmission(addr);
    return (wire->endTransmission() == 0);
}

// Read values from sensor
bool CMPS14Sensor::read(float &angle_deg, float &pitch_deg, float &roll_deg) {
    wire->beginTransmission(addr);
    wire->write(REG_ANGLE_16_H);
    if (wire->endTransmission(false) != 0) return false;

    const uint8_t toRead = 4;
    uint8_t n = wire->requestFrom(addr, toRead);
    if (n != toRead) return false;

    uint8_t hi = wire->read();
    uint8_t lo = wire->read();
    int8_t pitch = (int8_t)wire->read();
    int8_t roll  = (int8_t)wire->read();

    uint16_t ang10 = ((uint16_t)hi << 8) | lo;
    angle_deg = ((float)ang10) / 10.0f;
    pitch_deg = (float)pitch;
    roll_deg = (float)roll;

    return true;
}

// Send command byte to sensor
bool CMPS14Sensor::sendCommand(uint8_t cmd) {
    wire->beginTransmission(addr);
    wire->write(REG_CMD);
    wire->write(cmd);
    if (wire->endTransmission() != 0) return false;
    delay(23);  // Datasheet recommends 20 ms delay here
    wire->requestFrom(addr, (uint8_t)1);
    if (!wire->available()) return false;
    uint8_t b = wire->read();
    return (b == REG_ACK1 || b == REG_ACK2);
}

// Read byte from sensor's register
uint8_t CMPS14Sensor::readRegister(uint8_t reg) {
    wire->beginTransmission(addr);
    wire->write(reg);
    if (wire->endTransmission(false) != 0) return REG_NACK;
    wire->requestFrom(addr, (uint8_t)1);
    if (!wire->available()) return REG_NACK;
    return wire->read();
}

bool CMPS14Sensor::isAck(uint8_t byte) {
    return (byte == REG_ACK1 || byte == REG_ACK2);
}

bool CMPS14Sensor::isNack(uint8_t byte) {
    return (byte == REG_NACK); 
}
