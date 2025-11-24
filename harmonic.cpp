#include "harmonic.h"

// Solve A..E with least squares method from 8 given datapoints
HarmonicCoeffs fit_harmonic_from_8(const float* hdg_deg, const float* dev_deg) {
  double MTM[5][5] = {0};
  double MTy[5] = {0};
  for (int i=0; i<8; i++) {
    double th = hdg_deg[i] * M_PI/180.0;
    double v[5] = {1.0, sin(th), cos(th), sin(2*th), cos(2*th)};
    double y = dev_deg[i];
    for (int r=0; r<5; r++) {
      MTy[r] += v[r]*y;
      for (int c=0; c<5; c++) MTM[r][c] += v[r]*v[c];
    }
  }

  // Gauss elimination
  double A[5][6];
  for (int r=0; r<5; r++) {
    for (int c=0; c<5; c++) A[r][c] = MTM[r][c];
    A[r][5] = MTy[r];
  }
  for (int i=0; i<5; i++) {
    int piv = i;
    for (int r=i+1; r<5; r++)
      if (fabs(A[r][i]) > fabs(A[piv][i])) piv = r;
    if (piv != i)
      for (int c=i; c<6; c++) { double tmp = A[i][c]; A[i][c] = A[piv][c]; A[piv][c] = tmp; }
    double diag = A[i][i];
    if (fabs(diag) < 1e-9) continue;
    for (int c=i; c<6; c++) A[i][c] /= diag;
    for (int r=0; r<5; r++) {
      if (r == i) continue;
      double f = A[r][i];
      for (int c=i; c<6; c++) A[r][c] -= f * A[i][c];
    }
  }
  HarmonicCoeffs res = {A[0][5], A[1][5], A[2][5], A[3][5], A[4][5]};
  return res;
}

// Calculate deviation (deg) for given heading (deg) using harmonic model
float deviation_harm_deg(const HarmonicCoeffs& h, float hdg_deg) {
  float th = hdg_deg * M_PI / 180.0f;
  return h.A + h.B*sinf(th) + h.C*cosf(th) + h.D*sinf(2*th) + h.E*cosf(2*th);
}