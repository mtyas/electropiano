// Wurlitzer 200A amplifier chain: DK preamp with LDR tremolo in the feedback
// loop, volume network, power amplifier and speaker, 2x oversampled.
//
// C++ port of OpenWurli's amplifier DSP (crates/openwurli-dsp/src:
// dk_preamp_legacy.rs, tremolo.rs, oversampler.rs, power_amp.rs [behavioral,
// the shipping default], speaker.rs, engine.rs, tables.rs volume network).
// Copyright (c) the OpenWurli contributors, GPL-3.0-or-later.
// Upstream: https://github.com/hal0zer0/openwurli
//
// One structural difference: upstream drives the LDR from a melange-generated
// solver of spice/melange/wurli-tremolo.cir. Here that same netlist (Twin-T
// TR-3/TR-4 oscillator, R-18 -> LED -> R-17 collector load, identical device
// cards) is solved by a small backward-Euler Newton MNA solver written for this
// port, so the LED current — and everything downstream of it — follows the same
// circuit. It is checked against the deck's documented behaviour (~5.6 Hz,
// LED 0.37-2.28 mA) in tools/test_wurli_port.cpp.
#pragma once

#include "WurliVoice.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace phs::wurli {

// =============================================================================
// 2x polyphase IIR half-band oversampler (oversampler.rs)
// =============================================================================
class Oversampler {
public:
    void reset() {
        for (auto* b : {&upA_, &upB_, &downA_, &downB_}) b->fill(0.0);
        downDelay_ = 0.0;
    }
    void upsample(double x, double& o0, double& o1) {
        o0 = branch(upA_, kA, x);
        o1 = branch(upB_, kB, x);
    }
    double downsample(double i0, double i1) {
        const double a = branch(downA_, kA, i0);
        const double b = branch(downB_, kB, i1);
        const double out = (a + downDelay_) * 0.5;
        downDelay_ = b;
        return out;
    }

private:
    static constexpr double kA[3] = {0.036681502163648, 0.248030921580110, 0.643184620136480};
    static constexpr double kB[3] = {0.110377634768680, 0.420399304190880, 0.854640112701920};
    static double branch(std::array<double, 3>& st, const double (&c)[3], double x) {
        for (int i = 0; i < 3; ++i) {
            const double y = c[i] * x + st[i];
            st[i] = x - c[i] * y;
            x = y;
        }
        return x;
    }
    std::array<double, 3> upA_{}, upB_{}, downA_{}, downB_{};
    double downDelay_ = 0.0;
};

// =============================================================================
// Small dense linear algebra helpers
// =============================================================================
template <int N>
using Mat = std::array<std::array<double, N>, N>;
template <int N>
using Vec = std::array<double, N>;

template <int N>
inline Mat<N> invert(const Mat<N>& m) {
    double aug[N][2 * N];
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) { aug[i][j] = m[i][j]; aug[i][N + j] = (i == j) ? 1.0 : 0.0; }
    for (int col = 0; col < N; ++col) {
        int best = col;
        for (int r = col + 1; r < N; ++r) if (std::fabs(aug[r][col]) > std::fabs(aug[best][col])) best = r;
        if (best != col) for (int j = 0; j < 2 * N; ++j) std::swap(aug[col][j], aug[best][j]);
        const double piv = aug[col][col];
        for (int j = 0; j < 2 * N; ++j) aug[col][j] /= piv;
        for (int r = 0; r < N; ++r) {
            if (r == col) continue;
            const double f = aug[r][col];
            for (int j = 0; j < 2 * N; ++j) aug[r][j] -= f * aug[col][j];
        }
    }
    Mat<N> inv{};
    for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) inv[i][j] = aug[i][N + j];
    return inv;
}

// Solve A x = b in place (partial pivoting); returns false if singular
template <int N>
inline bool solveInPlace(Mat<N> a, Vec<N>& b) {
    for (int col = 0; col < N; ++col) {
        int best = col;
        for (int r = col + 1; r < N; ++r) if (std::fabs(a[r][col]) > std::fabs(a[best][col])) best = r;
        if (std::fabs(a[best][col]) < 1e-300) return false;
        if (best != col) {
            std::swap(a[col], a[best]);
            std::swap(b[col], b[best]);
        }
        for (int r = col + 1; r < N; ++r) {
            const double f = a[r][col] / a[col][col];
            if (f == 0.0) continue;
            for (int j = col; j < N; ++j) a[r][j] -= f * a[col][j];
            b[r] -= f * b[col];
        }
    }
    for (int i = N - 1; i >= 0; --i) {
        double s = b[i];
        for (int j = i + 1; j < N; ++j) s -= a[i][j] * b[j];
        b[i] = s / a[i][i];
    }
    return true;
}

// =============================================================================
// DK preamp: 9-node MNA, trapezoidal, Newton on the 2x2 BJT kernel
// (dk_preamp_legacy.rs — schematic #203720-S-3, drawn topology)
// =============================================================================
class DkPreamp {
public:
    static constexpr double kVcc = 14.5;
    static constexpr double kR1 = 22000.0, kR2 = 1000000.0, kR3 = 470000.0, kRe1 = 33000.0, kRc1 = 150000.0;
    static constexpr double kRe2a = 270.0, kRe2b = 820.0, kRc2 = 1800.0, kR9 = 6800.0, kR10 = 56000.0, kRload = 100000.0;
    static constexpr double kRinEff = kR1 * kR2 / (kR1 + kR2);
    static constexpr double kKinDiv = kR2 / (kR1 + kR2);
    static constexpr double kCin = 0.022e-6, kC2 = 220.0e-12, kC3 = 100.0e-12, kC4 = 100.0e-12;
    static constexpr double kCe1 = 4.7e-6, kCe2 = 22.0e-6, kC6 = 4.7e-6;

    // 2N5089 forward-active card
    static constexpr double kIs = 3.03e-14, kVt = 0.026, kNf = 1.005, kBf = 1434.0;
    static constexpr double kIse = 2.88e-15, kNe = 1.262, kIkf = 0.01358, kVbeMax = 0.85;

    enum Node { BASE1, EMIT1, COLL1, EMIT2, EMIT2B, COLL2, NODE_C6, OUT_NODE, FB, N };
    using M = Mat<N>;
    using V = Vec<N>;
    struct Inc { int node; double coeff; };
    static constexpr Inc kNv[2][2] = {{{BASE1, 1.0}, {EMIT1, -1.0}}, {{COLL1, 1.0}, {EMIT2, -1.0}}};
    static constexpr Inc kNic[2][2] = {{{EMIT1, 1.0}, {COLL1, -1.0}}, {{EMIT2, 1.0}, {COLL2, -1.0}}};
    static constexpr Inc kNib[2][2] = {{{EMIT1, 1.0}, {BASE1, -1.0}}, {{EMIT2, 1.0}, {COLL1, -1.0}}};

    double openCircuitOutputFactor() const { return (kR9 + kRload) / kRload; }

    void prepare(double fs) {
        const double twoOverT = 2.0 * fs;
        const double alphaCin = 2.0 * kRinEff * kCin * fs;
        gCin_ = (2.0 * kCin * fs) / (1.0 + alphaCin);
        cCin_ = (1.0 - alphaCin) / (1.0 + alphaCin);
        gc1pc_ = gCin_ * (1.0 + cCin_);

        M g{}; V w{};
        stampR(g, BASE1, EMIT2B, kR3);
        g[EMIT1][EMIT1] += 1.0 / kRe1;
        g[COLL1][COLL1] += 1.0 / kRc1; w[COLL1] += kVcc / kRc1;
        stampR(g, EMIT2, EMIT2B, kRe2a);
        g[EMIT2B][EMIT2B] += 1.0 / kRe2b;
        g[COLL2][COLL2] += 1.0 / kRc2; w[COLL2] += kVcc / kRc2;
        stampR(g, NODE_C6, OUT_NODE, kR9);
        stampR(g, NODE_C6, FB, kR10);
        g[OUT_NODE][OUT_NODE] += 1.0 / kRload;
        gDcBase_ = g;
        g[BASE1][BASE1] += gCin_;

        M c{};
        stampC(c, COLL1, BASE1, kC3);
        stampC(c, COLL2, COLL1, kC4);
        stampC(c, EMIT1, FB, kCe1);
        stampC(c, COLL2, NODE_C6, kC6);
        c[EMIT2][EMIT2] += kCe2;
        c[BASE1][BASE1] += kC2;

        M a{};
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) {
                a[i][j] = twoOverT * c[i][j] + g[i][j];
                aNeg_[i][j] = twoOverT * c[i][j] - g[i][j];
            }
        for (int i = 0; i < N; ++i) { twoW_[i] = 2.0 * w[i]; w_[i] = w[i]; }
        s_ = invert<N>(a);
        computeK(s_, kNic, kC_);
        computeK(s_, kNib, kB_);
        for (int i = 0; i < N; ++i) { sFbCol_[i] = s_[i][FB]; sFbRow_[i] = s_[FB][i]; }
        sFbFb_ = s_[FB][FB];
        for (int i = 0; i < 2; ++i) {
            nvSfb_[i] = 0.0;
            for (const auto& e : kNv[i]) nvSfb_[i] += e.coeff * sFbCol_[e.node];
            sfbNic_[i] = sfbNib_[i] = 0.0;
            for (const auto& e : kNic[i]) sfbNic_[i] += e.coeff * sFbRow_[e.node];
            for (const auto& e : kNib[i]) sfbNib_[i] += e.coeff * sFbRow_[e.node];
            for (int r = 0; r < N; ++r) {
                sNic_[i][r] = sNib_[i][r] = 0.0;
                for (const auto& e : kNic[i]) sNic_[i][r] += e.coeff * s_[r][e.node];
                for (const auto& e : kNib[i]) sNib_[i][r] += e.coeff * s_[r][e.node];
            }
        }
        rLdr_ = 1000000.0;
        reset();
    }

    void reset() {
        double vnl[2]; V vdc{};
        fullDcSolve(rLdr_, vnl, vdc);
        gLdr_ = gLdrPrev_ = 1.0 / rLdr_;
        double ic, ib, gc, gb;
        st_.v = vdc;
        for (int k = 0; k < 2; ++k) { bjt(vnl[k], ic, ib, gc, gb); st_.ic[k] = ic; st_.ib[k] = ib; st_.vnl[k] = vnl[k]; }
        st_.jCin = gCin_ * vdc[BASE1];
        st_.cinRhsPrev = st_.jCin;
    }

    void setLdrResistance(double r) {
        const double nr = std::max(r, 1000.0);
        if (std::fabs(nr - rLdr_) > 0.01) { rLdr_ = nr; gLdr_ = 1.0 / nr; }
    }

    double process(double input) {
        V rhs{};
        for (int i = 0; i < N; ++i) {
            double s = 0.0;
            for (int j = 0; j < N; ++j) s += aNeg_[i][j] * st_.v[j];
            rhs[i] = s;
        }
        rhs[FB] -= gLdrPrev_ * st_.v[FB];
        const double vinEff = input * kKinDiv;
        const double cinRhsNow = gCin_ * vinEff + st_.jCin;
        rhs[BASE1] += cinRhsNow + st_.cinRhsPrev;
        for (int j = 0; j < 2; ++j) {
            for (const auto& e : kNic[j]) rhs[e.node] += e.coeff * st_.ic[j];
            for (const auto& e : kNib[j]) rhs[e.node] += e.coeff * st_.ib[j];
        }
        for (int i = 0; i < N; ++i) rhs[i] += twoW_[i];

        V vPredBase{};
        for (int i = 0; i < N; ++i) {
            double s = 0.0;
            for (int j = 0; j < N; ++j) s += s_[i][j] * rhs[j];
            vPredBase[i] = s;
        }
        const double smK = gLdr_ / (1.0 + sFbFb_ * gLdr_);
        const double smV = smK * vPredBase[FB];
        V vPred{};
        for (int i = 0; i < N; ++i) vPred[i] = vPredBase[i] - smV * sFbCol_[i];
        const double p[2] = {vPred[BASE1] - vPred[EMIT1], vPred[COLL1] - vPred[EMIT2]};

        double kc[2][2], kb[2][2];
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) {
                kc[i][j] = kC_[i][j] - smK * nvSfb_[i] * sfbNic_[j];
                kb[i][j] = kB_[i][j] - smK * nvSfb_[i] * sfbNib_[j];
            }
        double vnl[2] = {st_.vnl[0], st_.vnl[1]};
        for (int it = 0; it < 6; ++it) {
            double ic0, ib0, gc0, gb0, ic1, ib1, gc1, gb1;
            bjt(vnl[0], ic0, ib0, gc0, gb0);
            bjt(vnl[1], ic1, ib1, gc1, gb1);
            const double f0 = vnl[0] - p[0] - kc[0][0] * ic0 - kc[0][1] * ic1 - kb[0][0] * ib0 - kb[0][1] * ib1;
            const double f1 = vnl[1] - p[1] - kc[1][0] * ic0 - kc[1][1] * ic1 - kb[1][0] * ib0 - kb[1][1] * ib1;
            if (std::fabs(f0) < 1e-9 && std::fabs(f1) < 1e-9) break;
            const double j00 = 1.0 - kc[0][0] * gc0 - kb[0][0] * gb0;
            const double j01 = -kc[0][1] * gc1 - kb[0][1] * gb1;
            const double j10 = -kc[1][0] * gc0 - kb[1][0] * gb0;
            const double j11 = 1.0 - kc[1][1] * gc1 - kb[1][1] * gb1;
            const double det = j00 * j11 - j01 * j10;
            if (std::fabs(det) < 1e-30) break;
            const double id = 1.0 / det;
            vnl[0] -= id * (j11 * f0 - j01 * f1);
            vnl[1] -= id * (j00 * f1 - j10 * f0);
        }
        double icN[2], ibN[2], dummy0, dummy1;
        bjt(vnl[0], icN[0], ibN[0], dummy0, dummy1);
        bjt(vnl[1], icN[1], ibN[1], dummy0, dummy1);
        const double sfbDot = sfbNic_[0] * icN[0] + sfbNic_[1] * icN[1] + sfbNib_[0] * ibN[0] + sfbNib_[1] * ibN[1];
        for (int i = 0; i < N; ++i) {
            const double sni = icN[0] * sNic_[0][i] + icN[1] * sNic_[1][i] + ibN[0] * sNib_[0][i] + ibN[1] * sNib_[1][i];
            st_.v[i] = vPred[i] + sni - smK * sfbDot * sFbCol_[i];
        }
        st_.cinRhsPrev = cinRhsNow;
        st_.jCin = -gc1pc_ * (vinEff - st_.v[BASE1]) - cCin_ * st_.jCin;
        for (int k = 0; k < 2; ++k) { st_.ic[k] = icN[k]; st_.ib[k] = ibN[k]; st_.vnl[k] = vnl[k]; }
        gLdrPrev_ = gLdr_;

        const double out = st_.v[OUT_NODE];
        if (!std::isfinite(out)) { reset(); return 0.0; }
        return out;
    }

    double nodeVoltage(int node) const { return st_.v[node]; }

    static void bjt(double vbe, double& ic, double& ib, double& gic, double& gib) {
        constexpr double vtf = kNf * kVt, neVt = kNe * kVt;
        const double v = std::clamp(vbe, -1.0, kVbeMax);
        const double ef = std::exp(v / vtf);
        const double icc = kIs * (ef - 1.0);
        const double q2 = (kIs / kIkf) * ef;
        const double root = std::sqrt(1.0 + 4.0 * q2);
        const double qb = 0.5 * (1.0 + root);
        ic = icc / qb;
        const double ee = std::exp(v / neVt);
        ib = icc / kBf + kIse * (ee - 1.0);
        const double dicc = kIs * ef / vtf;
        const double dqb = (q2 / vtf) / root;
        gic = (dicc * qb - icc * dqb) / (qb * qb);
        gib = dicc / kBf + kIse * ee / neVt;
    }

private:
    struct State { double jCin, cinRhsPrev; V v; double ic[2], ib[2], vnl[2]; };

    static void stampR(M& g, int i, int j, double r) {
        const double c = 1.0 / r;
        g[i][i] += c; g[j][j] += c; g[i][j] -= c; g[j][i] -= c;
    }
    static void stampC(M& c, int i, int j, double v) {
        c[i][i] += v; c[j][j] += v; c[i][j] -= v; c[j][i] -= v;
    }
    static void computeK(const M& s, const Inc (&ni)[2][2], double (&k)[2][2]) {
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) {
                double acc = 0.0;
                for (const auto& a : kNv[i]) for (const auto& b : ni[j]) acc += a.coeff * b.coeff * s[a.node][b.node];
                k[i][j] = acc;
            }
    }

    void fullDcSolve(double rLdr, double (&vnl)[2], V& vdc) const {
        M gFull = gDcBase_;
        gFull[FB][FB] += 1.0 / rLdr;
        const M sDc = invert<N>(gFull);
        double kc[2][2], kb[2][2];
        computeK(sDc, kNic, kc);
        computeK(sDc, kNib, kb);
        V sv{};
        for (int i = 0; i < N; ++i) { double s = 0.0; for (int j = 0; j < N; ++j) s += sDc[i][j] * w_[j]; sv[i] = s; }
        const double p[2] = {sv[BASE1] - sv[EMIT1], sv[COLL1] - sv[EMIT2]};
        vnl[0] = 0.56; vnl[1] = 0.66;
        for (int it = 0; it < 200; ++it) {
            double ic0, ib0, gc0, gb0, ic1, ib1, gc1, gb1;
            bjt(vnl[0], ic0, ib0, gc0, gb0);
            bjt(vnl[1], ic1, ib1, gc1, gb1);
            const double f0 = vnl[0] - p[0] - kc[0][0] * ic0 - kc[0][1] * ic1 - kb[0][0] * ib0 - kb[0][1] * ib1;
            const double f1 = vnl[1] - p[1] - kc[1][0] * ic0 - kc[1][1] * ic1 - kb[1][0] * ib0 - kb[1][1] * ib1;
            if (std::fabs(f0) < 1e-13 && std::fabs(f1) < 1e-13) break;
            const double j00 = 1.0 - kc[0][0] * gc0 - kb[0][0] * gb0;
            const double j01 = -kc[0][1] * gc1 - kb[0][1] * gb1;
            const double j10 = -kc[1][0] * gc0 - kb[1][0] * gb0;
            const double j11 = 1.0 - kc[1][1] * gc1 - kb[1][1] * gb1;
            const double id = 1.0 / (j00 * j11 - j01 * j10);
            const double maxStep = 2.0 * kVt;
            vnl[0] -= std::clamp(id * (j11 * f0 - j01 * f1), -maxStep, maxStep);
            vnl[1] -= std::clamp(id * (j00 * f1 - j10 * f0), -maxStep, maxStep);
        }
        double ic[2], ib[2], d0, d1;
        bjt(vnl[0], ic[0], ib[0], d0, d1);
        bjt(vnl[1], ic[1], ib[1], d0, d1);
        V rhs = w_;
        for (int j = 0; j < 2; ++j) {
            for (const auto& e : kNic[j]) rhs[e.node] += e.coeff * ic[j];
            for (const auto& e : kNib[j]) rhs[e.node] += e.coeff * ib[j];
        }
        for (int i = 0; i < N; ++i) { double s = 0.0; for (int j = 0; j < N; ++j) s += sDc[i][j] * rhs[j]; vdc[i] = s; }
    }

    M s_{}, aNeg_{}, gDcBase_{};
    V twoW_{}, w_{}, sFbCol_{}, sFbRow_{};
    double kC_[2][2]{}, kB_[2][2]{};
    V sNic_[2]{}, sNib_[2]{};
    double sFbFb_ = 0.0, nvSfb_[2]{}, sfbNic_[2]{}, sfbNib_[2]{};
    double gCin_ = 0.0, cCin_ = 0.0, gc1pc_ = 0.0;
    double rLdr_ = 1e6, gLdr_ = 1e-6, gLdrPrev_ = 1e-6;
    State st_{};
};

// =============================================================================
// Twin-T tremolo oscillator (spice/melange/wurli-tremolo.cir), backward Euler
// Unknowns: out, node_hp, base3, node_lp, emit3, leda, ledk (vcc is the 14.5 V rail)
// =============================================================================
class TwinTOscillator {
public:
    static constexpr double kVcc = 14.5;
    static constexpr double kVt = 0.025851991;       // kT/q at 27 C (melange)
    static constexpr double kQIs = 1.4e-14, kQBf = 155.0, kQBr = 3.0; // Q2N2924 deck card
    static constexpr double kLedIs = 2.2324e-29;      // LED_GAASP (TIL209A level fit), N = 1
    enum Node { OUT_NODE, HP, BASE3, LP, EMIT3, LEDA, LEDK, N };
    using M = Mat<N>;
    using V = Vec<N>;

    // dt: simulated time step (seconds). Scaling it scales the oscillation rate exactly,
    // which is what scaling the Twin-T capacitors would do.
    void setTimeStep(double dt) { dt_ = dt; }

    void reset() {
        // DC operating point (capacitors open), then a small kick so the oscillation starts
        v_ = {4.37, 0.0, 1.24, 2.80, 0.66, 13.41, 11.87};
        const double savedDt = dt_;
        dt_ = 1e9; // effectively a DC solve
        vPrev_ = v_;
        for (int i = 0; i < 40; ++i) { vPrev_ = v_; step(); }
        dt_ = savedDt;
        dcOp_ = v_;
        v_[HP] += 0.01;
        vPrev_ = v_;
    }

    void step() {
        vPrev_ = v_;
        V v = v_;
        for (int it = 0; it < 20; ++it) {
            M J{}; V F{};
            assemble(v, J, F);
            V dx = F;
            if (!solveInPlace<N>(J, dx)) break;
            double maxD = 0.0;
            for (int i = 0; i < N; ++i) {
                const double d = std::clamp(dx[i], -0.5, 0.5); // step limiting keeps the junctions sane
                v[i] -= d;
                maxD = std::max(maxD, std::fabs(d));
            }
            if (maxD < 1e-9) break;
        }
        for (const double x : v) if (!std::isfinite(x)) { v = dcOp_; break; }
        v_ = v;
    }

    double ledCurrentMa() const { return (kVcc - v_[LEDA]) / 680.0 * 1000.0; }
    const V& dcOperatingPoint() const { return dcOp_; }
    const V& voltages() const { return v_; }

private:
    static double safeExp(double x) { return std::exp(std::clamp(x, -40.0, 40.0)); }

    void assemble(const V& v, M& J, V& F) const {
        auto addG = [&](int a, int b, double g) {
            // conductance between a and b (b = -1 for ground)
            F[a] += g * (v[a] - (b >= 0 ? v[b] : 0.0)); J[a][a] += g;
            if (b >= 0) { F[b] -= g * (v[a] - v[b]); J[b][b] += g; J[a][b] -= g; J[b][a] -= g; }
        };
        auto addC = [&](int a, int b, double c) {
            const double g = c / dt_;
            const double dvPrev = vPrev_[a] - (b >= 0 ? vPrev_[b] : 0.0);
            const double dv = v[a] - (b >= 0 ? v[b] : 0.0);
            const double i = g * (dv - dvPrev);
            F[a] += i; J[a][a] += g;
            if (b >= 0) { F[b] -= i; J[b][b] += g; J[a][b] -= g; J[b][a] -= g; }
        };
        // Twin-T network
        addC(OUT_NODE, HP, 0.12e-6);
        addC(HP, BASE3, 0.12e-6);
        addG(HP, -1, 1.0 / 27000.0);
        addG(OUT_NODE, LP, 1.0 / 680000.0);
        addG(LP, BASE3, 1.0 / 680000.0);
        addC(LP, -1, 0.12e-6);
        addG(BASE3, -1, 1.0 / 680000.0);
        addG(EMIT3, -1, 1.0 / 10000.0);
        addG(OUT_NODE, -1, 1.0 / 10.0e6); // .input_impedance 10MEG
        addC(OUT_NODE, -1, 1.0e-12);      // Cpar
        // R-18 from the rail to the LED anode, R-17 from the cathode to the collector node
        F[LEDA] += (v[LEDA] - kVcc) / 680.0; J[LEDA][LEDA] += 1.0 / 680.0;
        addG(LEDK, OUT_NODE, 1.0 / 4700.0);
        // LED (Shockley, N = 1)
        {
            const double vd = v[LEDA] - v[LEDK];
            const double e = std::exp(std::min(vd / kVt, 80.0));
            const double id = kLedIs * (e - 1.0), gd = kLedIs * e / kVt + 1e-12;
            F[LEDA] += id; F[LEDK] -= id;
            J[LEDA][LEDA] += gd; J[LEDK][LEDK] += gd; J[LEDA][LEDK] -= gd; J[LEDK][LEDA] -= gd;
        }
        // Q3 (C=out, B=base3, E=emit3) and Q4 (C=out, B=emit3, E=gnd): Ebers-Moll transport
        bjt(v, OUT_NODE, BASE3, EMIT3, J, F);
        bjt(v, OUT_NODE, EMIT3, -1, J, F);
    }

    void bjt(const V& v, int c, int b, int e, M& J, V& F) const {
        const double vb = v[b], vc = v[c], ve = e >= 0 ? v[e] : 0.0;
        const double ebe = safeExp((vb - ve) / kVt), ebc = safeExp((vb - vc) / kVt);
        const double ic = kQIs * (ebe - ebc) - kQIs / kQBr * (ebc - 1.0);
        const double ib = kQIs / kQBf * (ebe - 1.0) + kQIs / kQBr * (ebc - 1.0);
        const double dIcDvbe = kQIs / kVt * ebe;
        const double dIcDvbc = -(kQIs / kVt) * ebc - kQIs / (kQBr * kVt) * ebc;
        const double dIbDvbe = kQIs / (kQBf * kVt) * ebe;
        const double dIbDvbc = kQIs / (kQBr * kVt) * ebc;
        // currents into the device: ic at collector, ib at base, -(ic+ib) at emitter
        const int nodes[3] = {c, b, e};
        // d/dv of (vbe, vbc) w.r.t. (vc, vb, ve)
        const double dVbe[3] = {0.0, 1.0, -1.0};
        const double dVbc[3] = {-1.0, 1.0, 0.0};
        const double cur[3] = {ic, ib, -(ic + ib)};
        const double dI[3][2] = {{dIcDvbe, dIcDvbc}, {dIbDvbe, dIbDvbc}, {-(dIcDvbe + dIbDvbe), -(dIcDvbc + dIbDvbc)}};
        for (int r = 0; r < 3; ++r) {
            if (nodes[r] < 0) continue;
            F[nodes[r]] += cur[r];
            for (int k = 0; k < 3; ++k) {
                if (nodes[k] < 0) continue;
                J[nodes[r]][nodes[k]] += dI[r][0] * dVbe[k] + dI[r][1] * dVbc[k];
            }
        }
    }

    double dt_ = 1.0 / 48000.0;
    V v_{}, vPrev_{}, dcOp_{};
};

// =============================================================================
// Tremolo: LED light law -> CdS LDR -> vibrato depth network (tremolo.rs)
// =============================================================================
class Tremolo {
public:
    static constexpr double kNaturalRateHz = 5.56; // rate of the deck as drawn (measured, R-17 = 4.7K)

    void prepare(double fs) {
        fs_ = fs;
        // The oscillator only oscillates at ~5.6 Hz, so it is stepped at ~6 kHz and the LED
        // drive is interpolated between steps (rate and LED swing verified against the deck).
        decim_ = std::max(1, static_cast<int>(std::lround(fs / 6000.0)));
        attack_ = std::exp(-1.0 / (0.0025 * fs));
        release_ = std::exp(-1.0 / (0.035 * fs));
        for (int k = 0; k < kLutN; ++k) lut_[k] = std::clamp(ledIntensity(kLedIFullMa * k / (kLutN - 1.0)), 0.0, 1.0);
        // LDR law R = exp(ln Rmax + (ln Rmin - ln Rmax) d^gamma), tabulated over the light level d
        for (int k = 0; k < kLdrLutN; ++k) {
            const double d = static_cast<double>(k) / (kLdrLutN - 1);
            ldrLut_[k] = k == 0 ? kRLdrMax : std::exp(kLnRMax + kLnMinMinusMax * std::pow(d, kGamma));
        }
        setRateScale(rateScale_);
        reset();
    }

    void reset() {
        osc_.setTimeStep(decim_ / fs_ * rateScale_);
        osc_.reset();
        // Settle the oscillator to its steady amplitude (as upstream does: 2 s)
        const int settle = static_cast<int>(2.0 * fs_ / decim_ / std::max(0.25, rateScale_));
        for (int i = 0; i < settle; ++i) osc_.step();
        lightPrev_ = lightNow_ = light(osc_.ledCurrentMa());
        phase_ = 0;
        envelope_ = 0.0;
        rLdr_ = kRLdrMax;
    }

    // 1.0 = the circuit's natural rate; time-scales the oscillator like scaling its capacitors
    void setRateScale(double s) {
        rateScale_ = std::clamp(s, 0.05, 4.0);
        osc_.setTimeStep(decim_ / fs_ * rateScale_);
    }
    void setDepth(double d) { depth_ = std::clamp(d, 0.0, 1.0); }

    // Returns the shunt impedance seen by the preamp's feedback junction
    double process() {
        // With the depth pot at zero the LDR is out of circuit (shuntImpedance ignores it), so
        // the oscillator pauses instead of being solved for nothing; it resumes where it stopped
        if (depth_ <= 0.0) return shuntImpedance();
        if (phase_ == 0) {
            lightPrev_ = lightNow_;
            osc_.step();
            lightNow_ = light(osc_.ledCurrentMa());
        }
        const double frac = static_cast<double>(phase_ + 1) / decim_;
        phase_ = (phase_ + 1) % decim_;
        const double drive = lightPrev_ + (lightNow_ - lightPrev_) * frac;

        const double coeff = drive > envelope_ ? attack_ : release_;
        envelope_ = drive + coeff * (envelope_ - drive);
        const double d = std::clamp(envelope_, 0.0, 1.0);
        if (d < 1e-6) {
            rLdr_ = kRLdrMax;
        } else {
            const double x = d * (kLdrLutN - 1);
            const int k = std::min(static_cast<int>(x), kLdrLutN - 2);
            rLdr_ = ldrLut_[k] + (x - k) * (ldrLut_[k + 1] - ldrLut_[k]);
        }
        return shuntImpedance();
    }

    double ledCurrentMa() const { return osc_.ledCurrentMa(); }
    const TwinTOscillator& oscillator() const { return osc_; }

private:
    static constexpr double kGamma = 0.9;
    static constexpr double kRLdrMin = 9000.0, kRLdrMax = 1000000.0;
    static constexpr double kRVibBridge = 18000.0, kRVibPot = 50000.0;
    static constexpr double kLedIFullMa = 2.279;
    static constexpr int kLutN = 256;
    static constexpr int kLdrLutN = 2048;
    static inline const double kLnRMax = std::log(kRLdrMax);
    static inline const double kLnMinMinusMax = std::log(kRLdrMin) - std::log(kRLdrMax);

    // TI TIL209A relative luminous intensity, piecewise power law, normalised at kLedIFullMa
    static double ledIntensity(double iMa) {
        if (iMa <= 0.0) return 0.0;
        static constexpr double law[5][2] = {{0.10, 1.36}, {1.00, 1.22}, {3.00, 1.02}, {10.00, 0.88}, {40.00, 0.88}};
        auto lnAt = [](double i) {
            double prevI = law[0][0], prevN = law[0][1];
            if (i <= prevI) return prevN * std::log(i / prevI);
            double acc = 0.0;
            for (int k = 1; k < 5; ++k) {
                acc += prevN * std::log(std::min(i, law[k][0]) / prevI);
                if (i <= law[k][0]) return acc;
                prevI = law[k][0];
                prevN = law[k][1];
            }
            return acc + prevN * std::log(i / prevI);
        };
        return std::exp(lnAt(iMa) - lnAt(kLedIFullMa));
    }

    double light(double iMa) const {
        const double x = std::clamp(iMa / kLedIFullMa, 0.0, 1.0) * (kLutN - 1);
        const int k = static_cast<int>(x);
        if (k + 1 >= kLutN) return lut_[kLutN - 1];
        const double f = x - k;
        return lut_[k] * (1.0 - f) + lut_[k + 1] * f;
    }

    double shuntImpedance() const {
        const double rUpper = kRVibPot * (1.0 - depth_), rLower = kRVibPot * depth_;
        const double top = rUpper > 0.0 ? rUpper * kRVibBridge / (rUpper + kRVibBridge) : 0.0;
        const double low = rLower > 0.0 ? rLower * rLdr_ / (rLower + rLdr_) : 0.0;
        return top + low;
    }

    TwinTOscillator osc_;
    std::array<double, kLutN> lut_{};
    std::array<double, kLdrLutN> ldrLut_{};
    double fs_ = 96000.0, rateScale_ = 1.0, depth_ = 0.5;
    int decim_ = 16, phase_ = 0;
    double lightPrev_ = 0.0, lightNow_ = 0.0;
    double attack_ = 0.0, release_ = 0.0, envelope_ = 0.0, rLdr_ = kRLdrMax;
};

// =============================================================================
// Power amplifier — behavioral closed-loop NR model, R-30/C-10 feedback shelf
// (power_amp.rs, `legacy-power-amp`: the shipping default upstream)
// =============================================================================
class PowerAmp {
public:
    static constexpr double kHeadroomV = 22.0;

    void prepare(double fs) {
        constexpr double r30 = 220.0, r31 = 15000.0, c10 = 22e-6;
        const double k = 2.0 * fs;
        const double tauZero = r30 * c10, tauPole = (r30 + r31) * c10;
        const double a0 = 1.0 + tauPole * k;
        b0_ = (1.0 + tauZero * k) / a0;
        b1_ = (1.0 - tauZero * k) / a0;
        a1_ = (1.0 - tauPole * k) / a0;
        reset();
    }
    void reset() { y1_ = 0.0; fbPrev_ = 0.0; }

    // Returns the output normalised to the rail (+-1.0 = +-22 V)
    double process(double input) {
        constexpr double kOpenLoopGain = 19000.0, kTol = 1e-6;
        const double fbHist = b1_ * y1_ - a1_ * fbPrev_;
        double y = std::clamp((input - fbHist) * kOpenLoopGain / (1.0 + kOpenLoopGain * b0_),
                              -kHeadroomV + kTol, kHeadroomV - kTol);
        for (int it = 0; it < 8; ++it) {
            const double v = kOpenLoopGain * (input - (b0_ * y + fbHist));
            double f, df;
            forward(v, f, df);
            const double delta = (y - f) / (1.0 + kOpenLoopGain * b0_ * df);
            y -= delta;
            if (std::fabs(delta) < kTol) break;
        }
        fbPrev_ = b0_ * y + fbHist;
        y1_ = y;
        return y / kHeadroomV;
    }

private:
    // Gaussian crossover notch, then tanh rail saturation
    static void forward(double v, double& f, double& df) {
        constexpr double vt = 0.013, q = 0.1;
        const double vtSq = vt * vt;
        const double e = std::exp(-v * v / vtSq);
        const double cross = q + (1.0 - q) * (1.0 - e);
        const double dCross = cross + v * (1.0 - q) * (2.0 * v / vtSq) * e;
        const double t = std::tanh(v * cross / kHeadroomV);
        f = kHeadroomV * t;
        df = (1.0 - t * t) * dCross;
    }
    double b0_ = 1.0, b1_ = 0.0, a1_ = 0.0, y1_ = 0.0, fbPrev_ = 0.0;
};

// =============================================================================
// Speaker cabinet (speaker.rs): polynomial cone nonlinearity, Xmax tanh,
// thermal compression, HPF/LPF. character 0 = true bypass, 1 = authentic.
// =============================================================================
class Speaker {
public:
    void prepare(double fs) {
        fs_ = fs;
        thermalAlpha_ = 1.0 / (5.0 * fs);
        character_ = -1.0;
        setCharacter(0.0);
        reset();
    }
    void reset() { hpf_.reset(); lpf_.reset(); thermal_ = 0.0; }
    void setCharacter(double c) {
        c = std::clamp(c, 0.0, 1.0);
        if (std::fabs(c - character_) <= 0.002) return;
        character_ = c;
        hpf_.setHighpass(20.0 * std::pow(30.0 / 20.0, c), 0.75, fs_);
        lpf_.setLowpass(20000.0 * std::pow(5500.0 / 20000.0, c), 0.707, fs_);
        a2_ = 0.2 * c;
        a3_ = 0.6 * c;
        thermalCoeff_ = 2.0 * c;
    }
    double process(double x) {
        const double x2 = x * x;
        const double shaped = (x + a2_ * x2 + a3_ * x2 * x) / (1.0 + a2_ + a3_);
        const double limited = character_ < 0.001 ? shaped : std::tanh(shaped);
        thermal_ += (x2 - thermal_) * thermalAlpha_;
        const double gain = 1.0 / (1.0 + thermalCoeff_ * std::sqrt(thermal_));
        const double out = lpf_.process(hpf_.process(limited * gain));
        return character_ < 0.001 ? x : out;
    }

private:
    Biquad hpf_, lpf_;
    double fs_ = 48000.0, character_ = 0.0, a2_ = 0.0, a3_ = 0.0;
    double thermalCoeff_ = 0.0, thermalAlpha_ = 0.0, thermal_ = 0.0;
};

// =============================================================================
// Volume network between the preamp and the power amp (tables.rs)
// =============================================================================
inline constexpr double kR11ReedBarDefault = 17200.0;
inline double audioTaper(double pos) {
    const double p = std::clamp(pos, 0.0, 1.0);
    return p <= 0.5 ? 0.15 * (p / 0.5) : 0.15 + 0.85 * ((p - 0.5) / 0.5);
}
inline void volumeNetwork(double vol, double r11, double& gain, double& poleHz) {
    constexpr double kPot = 10000.0, kR9 = 6800.0, kRin = 15000.0, kC9 = 1.0e-9;
    const double rLower = kPot * audioTaper(vol), rUpper = kPot - rLower;
    const double rLowEff = rLower <= 0.0 ? 0.0 : rLower * kRin / (rLower + kRin);
    gain = rLowEff / (kR9 + r11 + rUpper + rLowEff);
    auto par = [](double a, double b) { return a * b / (a + b); };
    const double rTh = rLower <= 0.0 ? 0.0 : par(par(kR9 + r11 + rUpper, rLower), kRin);
    poleHz = rTh <= 0.0 ? 1e12 : 1.0 / (kTau * rTh * kC9);
}

// =============================================================================
// The assembled amp (engine.rs render path): preamp + tremolo at 2x, volume
// network and C-9 pole, power amp, then speaker at the host rate.
// =============================================================================
class Amp200A {
public:
    // Plugin output alignment upstream (OUTPUT_ALIGNMENT_DB): rail 1.0 -> +14 dB
    static constexpr double kOutputAlignmentDb = 14.0;

    void prepare(double fs) {
        fs_ = fs;
        oversample_ = fs < 88200.0;
        osFs_ = oversample_ ? 2.0 * fs : fs;
        preamp_.prepare(osFs_);
        tremolo_.prepare(osFs_);
        powerAmp_.prepare(osFs_);
        speaker_.prepare(fs);
        oversampler_.reset();
        rampSamples_ = std::max(1, static_cast<int>(fs * 0.005));
        volume_.snap(0.8); depth_.snap(0.5); character_.snap(0.0);
        c9_ = 0.0;
        cachedVol_ = -1.0;
        alignment_ = std::pow(10.0, kOutputAlignmentDb / 20.0);
        // Warm-up as upstream: let couplings settle before the first note
        for (int i = 0; i < static_cast<int>(fs * 0.6); ++i) process(0.0);
    }

    void reset() {
        preamp_.reset();
        tremolo_.reset();
        powerAmp_.reset();
        speaker_.reset();
        oversampler_.reset();
        c9_ = 0.0;
    }

    void setVolume(double v) { volume_.set(v, rampSamples_); }
    void setTremoloDepth(double d) { depth_.set(d, rampSamples_); }
    void setTremoloRateScale(double s) { tremolo_.setRateScale(s); }
    void setSpeakerCharacter(double c) { character_.set(c, rampSamples_); }

    // input: summed pickup voltage of all voices (volts); output: aligned plugin level
    double process(double input) {
        const double depth = depth_.next();
        const double vol = volume_.next();
        tremolo_.setDepth(depth);
        if (vol != cachedVol_) {
            // The volume network only changes while the pot moves
            cachedVol_ = vol;
            double gain, poleHz;
            volumeNetwork(vol, kR11ReedBarDefault, gain, poleHz);
            driveGain_ = preamp_.openCircuitOutputFactor() * gain;
            c9Alpha_ = poleHz >= 0.5 * osFs_ ? 1.0 : 1.0 - std::exp(-kTau * poleHz / osFs_);
        }
        const double driveGain = driveGain_, c9Alpha = c9Alpha_;

        double y;
        if (oversample_) {
            double u[2];
            oversampler_.upsample(input, u[0], u[1]);
            for (double& s : u) {
                preamp_.setLdrResistance(tremolo_.process());
                c9_ += c9Alpha * (preamp_.process(s) * driveGain - c9_);
                s = powerAmp_.process(c9_);
            }
            y = oversampler_.downsample(u[0], u[1]);
        } else {
            preamp_.setLdrResistance(tremolo_.process());
            c9_ += c9Alpha * (preamp_.process(input) * driveGain - c9_);
            y = powerAmp_.process(c9_);
        }
        speaker_.setCharacter(character_.next());
        const double out = speaker_.process(y) * alignment_;
        if (!std::isfinite(out)) { reset(); return 0.0; }
        return out;
    }

    const Tremolo& tremolo() const { return tremolo_; }

private:
    struct Smoother {
        double cur = 0.0, target = 0.0, step = 0.0;
        int remaining = 0;
        void snap(double v) { cur = target = v; remaining = 0; }
        void set(double v, int ramp) {
            if (std::fabs(v - target) < 1e-9) return;
            target = v;
            step = (target - cur) / ramp;
            remaining = ramp;
        }
        double next() {
            if (remaining > 0 && --remaining == 0) cur = target;
            else if (remaining > 0) cur += step;
            return cur;
        }
    };

    DkPreamp preamp_;
    Tremolo tremolo_;
    PowerAmp powerAmp_;
    Speaker speaker_;
    Oversampler oversampler_;
    Smoother volume_, depth_, character_;
    double fs_ = 48000.0, osFs_ = 96000.0, c9_ = 0.0, alignment_ = 1.0;
    double cachedVol_ = -1.0, driveGain_ = 0.0, c9Alpha_ = 1.0;
    bool oversample_ = true;
    int rampSamples_ = 240;
};

} // namespace phs::wurli
