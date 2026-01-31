#include "harmonic.h"

// === G L O B A L  C O R E  F U N C T I O N S ===
//
// Solve A..E with least squares method from 8 given datapoints
//
// This function calculates 5 harmonic coefficients (A, B, C, D, E) that best fit
// the mathematical model: deviation(θ) = A + B·sin(θ) + C·cos(θ) + D·sin(2θ) + E·cos(2θ)
//
// Input: 8 measured deviation values at cardinal and intercardinal directions
//        (0°, 45°, 90°, 135°, 180°, 225°, 270°, 315°)
// Output: Coefficients that produce a smooth curve through all 8 points
//
// Method: Least squares regression using Gauss-Jordan elimination
//         (minimizes the error between measured values and the fitted curve)
// Author: This function has been provided by ChatGPT and reviewed by Claude Code.

HarmonicCoeffs computeHarmonicCoeffs(const float* dev_deg) {
  
  // === STEP 1: Build the normal equations (M^T·M and M^T·y) ===
  // We have 8 measurements but only 5 unknowns (A,B,C,D,E), so we use
  // least squares to find the best fit that minimizes error
  
  float MTM[5][5] = {0}; // M^T * M: 5x5 matrix (left side of equation)
  float MTy[5] = {0};    // M^T * y: 5x1 vector (right side of equation)

  for (int i=0; i<8; i++) {
    float th = headings_deg[i] * M_PI/180.0;

    // Model vector: [1, sin(θ), cos(θ), sin(2θ), cos(2θ)]
    // This represents the basis functions for our harmonic model

    float v[5] = {1.0, sin(th), cos(th), sin(2*th), cos(2*th)};
    float y = dev_deg[i]; // The measured deviation at this heading
    
    // Accumulate M^T·M and M^T·y (normal equations for least squares)
    for (int r=0; r<5; r++) {
      MTy[r] += v[r]*y; // Build M^T * y
      for (int c=0; c<5; c++) MTM[r][c] += v[r]*v[c]; // Build M^T * M
    }
  }

  // === STEP 2: Create augmented matrix for Gauss-Jordan elimination ===
  // Format: [MTM | MTy] becomes a 5x6 matrix
  // We'll transform the left side to identity matrix, leaving solution on right

  float A[5][6]; // Augmented matrix [MTM | MTy]
  for (int r=0; r<5; r++) {
    for (int c=0; c<5; c++) A[r][c] = MTM[r][c]; // Copy MTM to left side
    A[r][5] = MTy[r]; // Add MTy as the rightmost column
  }

  // === STEP 3: Gauss-Jordan elimination with partial pivoting ===
  // This transforms the matrix to reduced row echelon form (RREF):
  // [1 0 0 0 0 | A]
  // [0 1 0 0 0 | B]
  // [0 0 1 0 0 | C]
  // [0 0 0 1 0 | D]
  // [0 0 0 0 1 | E]

  for (int i=0; i<5; i++) { // Process each column

    // STEP 3.1: Find pivot (largest absolute value in column i)
    // This improves numerical stability by avoiding division by small numbers

    int piv = i;
    for (int r=i+1; r<5; r++)
      if (fabs(A[r][i]) > fabs(A[piv][i])) piv = r; // Found a larger pivot

    // STEP 3.2: Swap rows if needed (move pivot to diagonal)

    if (piv != i)
      for (int c=i; c<6; c++) { float tmp = A[i][c]; A[i][c] = A[piv][c]; A[piv][c] = tmp; }

    // STEP 3.3: Normalize pivot row (make diagonal element = 1)

    float diag = A[i][i];
    if (fabs(diag) < 1e-9) continue; // Skip if nearly zero (singular matrix)
    for (int c=i; c<6; c++) A[i][c] /= diag; // Divide entire row by diagonal element

    // STEP 3.4: Eliminate column i in all other rows (make them 0)

    for (int r=0; r<5; r++) {
      if (r == i) continue; // Skip the pivot row itself
      float f = A[r][i]; // Factor to eliminate this element
      for (int c=i; c<6; c++) A[r][c] -= f * A[i][c]; // Row_r = Row_r - factor * Pivot_row
    }
  }

  // === STEP 4: Extract solution from rightmost column ===
  // After elimination, the augmented matrix is [I | solution]
  // The last column now contains our coefficients A, B, C, D, E

  HarmonicCoeffs res = {A[0][5], A[1][5], A[2][5], A[3][5], A[4][5]};
  return res;
}

// Calculate deviation (deg) for given heading (deg) using harmonic model
//
// This function evaluates the harmonic model at any heading angle to get
// the interpolated deviation value. Uses the 5 coefficients (A,B,C,D,E)
// calculated by computeHarmonicCoeffs().
//
// The model equation: deviation(θ) = A + B·sin(θ) + C·cos(θ) + D·sin(2θ) + E·cos(2θ)
//
// Input:  h       - The 5 harmonic coefficients (A, B, C, D, E)
//         hdg_deg - Compass heading in degrees (0-360)
// Output: Interpolated deviation value in degrees at the given heading
//
// Example: If you measured deviations at 8 cardinal points, this function
//          gives you a smooth estimate at any heading like 37° or 213°
// Author: This function has been provided by ChatGPT and reviewed by Claude Code.

float computeDeviation(const HarmonicCoeffs& h, float hdg_deg) {
  float th = hdg_deg * M_PI / 180.0f;

  // Evaluate the harmonic model: sum of constant, 1st and 2nd harmonics

  return h.A              // Constant offset
       + h.B*sinf(th)     // 1st harmonic sine component
       + h.C*cosf(th)     // 1st harmonic cosine component
       + h.D*sinf(2*th)   // 2nd harmonic sine component (captures asymmetry)
       + h.E*cosf(2*th);  // 2nd harmonic cosine component (captures elliptical effects)
}

// === D E V I A T I O N  L O O K U P  T A B L E  C L A S S ===

// === P U B L I C ===

// Build the lookup table by computing deviation for each 1° of 360°
void DeviationLookup::build(const HarmonicCoeffs &hc) {

  for (int i = 0; i < SIZE; i++) {
    float hdg_deg = (float)i;
    float th = hdg_deg * M_PI / 180.0f;
    lut[i] = hc.A + hc.B*sinf(th) + hc.C*cosf(th) + hc.D*sinf(2*th) + hc.E*cosf(2*th);
  }
  valid = true;
}

// Return an interpolated value from lookup table based on HDG(C)
float DeviationLookup::lookup(float compass_deg) const {
  if (!valid) return 0.0f;

  // Validate input
  if (!validf(compass_deg)) return 0.0f;

  // Normalize to [0, 360) range
  while (compass_deg >= 360.0f) compass_deg -= 360.0f;
  while (compass_deg < 0.0f) compass_deg += 360.0f;

  // Interpolate (linear)
  int idx = (int)compass_deg;
  float frac = compass_deg - idx;

  // Ensure idx is within bounds
  if (idx < 0 || idx >= SIZE) return 0.0f;

  int next = (idx + 1) % SIZE;
  float result = lut[idx] * (1.0f - frac) + lut[next] * frac;

  return result;
}

// Return shortest arc on 360° (for instance 359° to 001° is 2° not 358°)
float computeAngDiffRad(float a, float b) {
    float d = a - b;
    while (d > M_PI) d -= 2.0f * M_PI;
    while (d <= -M_PI) d += 2.0f * M_PI;
    return d;
}