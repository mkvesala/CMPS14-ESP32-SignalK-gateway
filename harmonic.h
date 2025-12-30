#pragma once

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <Arduino.h>

// === G L O B A L  C O R E  S T R U C T U R E S ===
//
// - Global data struct to maintain 5 coefficients for deviation calculation
// - Coeffs used in: deviation(θ) = A + B·sin(θ) + C·cos(θ) + D·sin(2θ) + E·cos(2θ)
// - Global const to maintain the degree values of 8 cardinal/intercardinal points
//   N, NE, E, SE, S, SW, W, NW

struct HarmonicCoeffs {
  float A, B, C, D, E;
};

static constexpr float headings_deg[8] = { 0, 45, 90, 135, 180, 225, 270, 315 };

// === G L O B A L  C O R E  F U N C T I O N S ===
//
// - Compute 5 coeffs from measured deviations at 8 cardinal/intercardinal points
// - Compute deviation based on the coeffs at any heading (degrees)
// - Inline helper to check float validity

HarmonicCoeffs computeHarmonicCoeffs(const float* dev_deg);
float computeDeviation(const HarmonicCoeffs& h, float hdg_deg);
inline bool validf(float x) { return !isnan(x) && isfinite(x); }

// === D E V I A T I O N  L O O K U P  T A B L E  C L A S S ===
//
// - Simple class DeviationLookup - provides the lookup table for computed deviations
// - Lookup table for each 1° over 360°
// - Build takes the 5 coeffs and computes the deviation for each 1° into the lookup table
// - Lookup returns a deviation at any heading
//   based on linear interpolation between two deviations in the lookup table

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