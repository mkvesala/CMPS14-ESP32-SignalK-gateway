#include "globals.h"


const uint8_t CMPS14_ADDR                 = 0x60;        // I2C address of CMPS14

// SH-ESP32 default pins for I2C
const uint8_t I2C_SDA = 16;
const uint8_t I2C_SCL = 17;

// Webserver
WebServer server(80);

