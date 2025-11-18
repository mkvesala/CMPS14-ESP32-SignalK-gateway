#pragma once

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <Arduino.h>

struct HarmonicCoeffs {
  float A, B, C, D, E;
};

HarmonicCoeffs fit_harmonic_from_8(const float* hdg_deg, const float* dev_deg);
float deviation_harm_deg(const HarmonicCoeffs& h, float hdg_deg);