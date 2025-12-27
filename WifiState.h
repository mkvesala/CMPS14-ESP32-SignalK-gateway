#pragma once

#include <Arduino.h>

enum class WifiState {
    INIT, CONNECTING, CONNECTED, FAILED, DISCONNECTED, OFF
};