#pragma once

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <Arduino.h>

struct HarmonicCoeffs {
  float A, B, C, D, E;
};

static constexpr float headings_deg[8] = { 0, 45, 90, 135, 180, 225, 270, 315 }; // Cardinal and intercardinal directions N, NE, E, SE, S, SW, W, NE in deg

HarmonicCoeffs computeHarmonicCoeffs(const float* dev_deg);
float computeDeviation(const HarmonicCoeffs& h, float hdg_deg);

// Return float validity
inline bool validf(float x) { return !isnan(x) && isfinite(x); }