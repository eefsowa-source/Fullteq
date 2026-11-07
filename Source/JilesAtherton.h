#pragma once

#include <algorithm>
#include <cmath>

namespace fullteq
{

/** Jiles-Atherton magnetic core model.

    A single differential equation now describes the hysteresis loop, the
    approach to saturation and the loss that grows with frequency. The previous
    implementation was a memoryless tanh plus one scalar play operator whose loop
    width was fixed, so 35 Hz and 200 Hz distorted by exactly the same amount.
    A real core cannot behave that way: the loop widens as the driving field
    moves faster.

    The state is the magnetisation M, integrated against the applied field H:

      He    = H + alpha*M                    mean-field effective field
      Man   = Ms * L(He/a)                   anhysteretic (Langevin) curve
      delta = sign(dH/dt)                    which way the field is going
      geom  = k*delta - alpha*(Man - M)      sign tracks the branch
      dM    = (Man - M) / (geom * xi)        xi >= 1 slows the approach
                                              (this is the loss; xi = 1 is lossless)

    The loss factor xi is where the frequency dependence lives. It carries two
    contributions, both measured against the field's rate of change:

      eddy current   xi += |gammaE * dH/dt|
      viscosity      xi += kTau * |dH/dt|

    A slow field (30 Hz) leaves xi near 1 and the loop is narrow; a fast field
    leaves it wide. That is the behaviour the earlier model could not produce.

    Parameters default to M6 grain-oriented silicon steel, the lamination a
    Pultec-era output transformer would have used. They are material constants,
    not knobs; drive and low boost act on the field H.
*/
struct JilesAthertonCore
{
    // Material parameters (M6 grain-oriented silicon steel, approximate fit).
    static constexpr double saturationMagnetisation = 1.6e6;  // Ms    [A/m]
    static constexpr double fieldAtInflection       = 60.0;   // a     [A/m]
    static constexpr double coerciveField           = 40.0;   // k     [A/m]
    static constexpr double reversibleFraction      = 0.20;   // c
    static constexpr double meanFieldCoupling       = 1.0e-3; // alpha

    // Frequency-dependent loss, in units of "field rate" per field.
    static constexpr double eddyCoefficient         = 0.8e-3; // [s]
    static constexpr double viscosityCoefficient    = 1.2e-3; // [s]

    /** Per-channel state. */
    struct State
    {
        double magnetisation = 0.0;   // M
        double previousH = 0.0;
        bool started = false;
    };

    /** Anhysteretic magnetisation for an effective field. */
    static double anhysteretic (double effectiveField)
    {
        const double x = effectiveField / fieldAtInflection;
        const double ax = std::abs (x);
        // coth(x) - 1/x is 0/0 at the origin; the series is exact below 1e-2.
        const double shape = ax < 1.0e-2
                                 ? x / 3.0
                                 : (x < 0.0 ? -1.0 : 1.0) * (1.0 / std::tanh (ax) - 1.0 / ax);
        return saturationMagnetisation * shape;
    }

    /** Advance the state by h seconds of applied field H (A/m). Returns M. */
    static double step (State& s, double H, double h)
    {
        const double dH = s.started ? (H - s.previousH) / h : 0.0;   // A/m per second
        s.previousH = H;
        s.started = true;

        const double He = H + meanFieldCoupling * s.magnetisation;
        const double Man = anhysteretic (He);

        // The loop is defined per branch: delta follows the field's direction,
        // and the geometric factor must keep the same sign so the core can
        // actually move. Clamping the magnitude to |delta| preserves the sign.
        const double delta = dH > 0.0 ? 1.0 : (dH < 0.0 ? -1.0 : (H >= 0.0 ? 1.0 : -1.0));
        const double geom = coerciveField * delta - meanFieldCoupling * (Man - s.magnetisation);
        const double safeGeom = std::copysign (std::max (std::abs (geom), coerciveField * 1.0e-2), delta);

        // Loss factor: 1 when the field is static, grows with its rate. This is
        // the entire frequency dependence - a slow field leaves the loop narrow,
        // a fast one widens it.
        const double rate = std::abs (dH);
        const double xi = 1.0 + (eddyCoefficient + viscosityCoefficient) * rate;

        const double dM = (Man - s.magnetisation) / (safeGeom * xi);

        s.magnetisation = std::clamp (s.magnetisation + dM,
                                      -saturationMagnetisation, saturationMagnetisation);
        return s.magnetisation;
    }
};

} // namespace fullteq
