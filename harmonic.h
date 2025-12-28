#pragma once

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <Arduino.h>

// === G L O B A L  C O R E  S T R U C T U R E S ===

struct HarmonicCoeffs {
  float A, B, C, D, E;
};

static constexpr float headings_deg[8] = { 0, 45, 90, 135, 180, 225, 270, 315 }; // Cardinal and intercardinal directions N, NE, E, SE, S, SW, W, NE in deg

// === G L O B A L  C O R E  F U N C T I O N S ===

HarmonicCoeffs computeHarmonicCoeffs(const float* dev_deg);
float computeDeviation(const HarmonicCoeffs& h, float hdg_deg);
inline bool validf(float x) { return !isnan(x) && isfinite(x); }

// === D E V I A T I O N  L O O K U P  T A B L E  C L A S S ===

class DeviationLookup {

public:

  explicit DeviationLookup() {
    for (int i = 0; i < SIZE; i++) {
      lut[i] = 0.0f;
    }
  }

  void build(const HarmonicCoeffs &hc);
  float lookup(float compass_deg) const;

private:

  static constexpr int SIZE = 360;
  float lut[SIZE];
  bool valid = false;

};