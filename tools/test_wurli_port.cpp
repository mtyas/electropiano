// Verifies the C++ port of OpenWurli against the acceptance targets that
// OpenWurli's own Rust tests assert (SPICE cross-check figures), plus the
// Twin-T tremolo deck's documented behaviour.
//
// Build: make test-wurli
#include "../src/dsp/wurli/WurliAmp.hpp"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace phs::wurli;

static int gFailures = 0;
static void check(bool ok, const char* what, double got, double want, double tol) {
    std::printf("  [%s] %-52s got %.5g  want %.5g +/- %.3g\n", ok ? " OK " : "FAIL", what, got, want, tol);
    if (!ok) ++gFailures;
}
static void near(const char* what, double got, double want, double tol) { check(std::fabs(got - want) <= tol, what, got, want, tol); }

static double measureGainDb(double freq, double rLdr, double sr) {
    DkPreamp p;
    p.prepare(sr);
    p.setLdrResistance(rLdr);
    p.reset();
    const double amp = 0.001;
    const int settle = static_cast<int>(sr * 0.4), measure = static_cast<int>(sr * 0.2);
    for (int i = 0; i < settle; ++i) p.process(amp * std::sin(kTau * freq * i / sr));
    double peak = 0.0;
    for (int i = 0; i < measure; ++i) peak = std::max(peak, std::fabs(p.process(amp * std::sin(kTau * freq * (settle + i) / sr))));
    return 20.0 * std::log10(peak / amp);
}

int main() {
    std::printf("[1] Pickup law splines vs generator check points\n");
    {
        struct T { const PickupLawTable* t; const double (*chk)[2]; int n; const char* name; };
        const T tables[4] = {{&R01_14, R01_14_CHECK, 17, "R01_14"}, {&R15_20, R15_20_CHECK, 17, "R15_20"},
                             {&R21_42, R21_42_CHECK, 16, "R21_42"}, {&R43_64, R43_64_CHECK, 16, "R43_64"}};
        for (const auto& t : tables) {
            double maxErr = 0.0;
            for (int i = 0; i < t.n; ++i) maxErr = std::max(maxErr, std::fabs(evalLaw(*t.t, t.chk[i][0]) - t.chk[i][1]));
            check(maxErr < 1e-9, t.name, maxErr, 0.0, 1e-9);
        }
    }

    std::printf("[2] Pickup network: small-signal one-pole HPF at PICKUP_FC (upstream: 0.011 dB)\n");
    {
        const double sr = 96000.0;
        double worst = 0.0;
        for (double f : {100.0, 300.0, 880.0, 3000.0, 10000.0}) {
            ElectrostaticPickup pk;
            pk.init(sr, 60, 0.001); // tiny swing: linear regime
            double peakIn = 0.0, peakOut = 0.0;
            const int n = static_cast<int>(sr * 0.5);
            for (int i = 0; i < n; ++i) {
                const double x = std::sin(kTau * f * i / sr);
                const double y = pk.process(x);
                if (i > n / 2) peakOut = std::max(peakOut, std::fabs(y));
                if (i > n / 2) peakIn = std::max(peakIn, std::fabs(x));
            }
            // reference: same source through an ideal one-pole HPF -> ratio to its HF asymptote
            const double hpf = f / std::sqrt(f * f + kPickupFc * kPickupFc);
            static double hfRef = 0.0;
            if (f == 10000.0) hfRef = peakOut / hpf;
            static std::vector<std::pair<double, double>> pts;
            pts.push_back({hpf, peakOut});
            if (f == 10000.0) {
                for (auto& p : pts) worst = std::max(worst, std::fabs(20.0 * std::log10((p.second / hfRef) / p.first)));
            }
        }
        check(worst < 0.05, "max deviation from one-pole HPF (dB)", worst, 0.0, 0.05);
        near("PICKUP_FC (Hz)", kPickupFc, 880.5, 0.01);
    }

    std::printf("[3] DK preamp DC operating point (SPICE cross-check, +/-5 mV)\n");
    {
        DkPreamp p;
        p.prepare(88200.0);
        near("TR-1 base", p.nodeVoltage(DkPreamp::BASE1), 2.651, 0.005);
        near("TR-1 emitter", p.nodeVoltage(DkPreamp::EMIT1), 2.093, 0.005);
        near("TR-1 collector", p.nodeVoltage(DkPreamp::COLL1), 4.270, 0.005);
        near("TR-2 emitter", p.nodeVoltage(DkPreamp::EMIT2), 3.602, 0.005);
        near("R-7/R-8 junction", p.nodeVoltage(DkPreamp::EMIT2B), 2.710, 0.005);
        near("TR-2 collector", p.nodeVoltage(DkPreamp::COLL2), 8.561, 0.005);
        near("out (behind C-6) DC", p.nodeVoltage(DkPreamp::OUT_NODE), 0.0, 1e-9);
    }

    std::printf("[4] DK preamp AC gain\n");
    {
        near("1 kHz gain, R_ldr 12K (dB)", measureGainDb(1000.0, 12000.0, 88200.0), 15.54, 0.3);
        const double gLo = measureGainDb(1000.0, 1e6, 88200.0);
        near("1 kHz gain, R_ldr 1M (dB, upstream ~7.0)", gLo, 7.0, 0.6);
    }

    std::printf("[5] Twin-T tremolo oscillator (wurli-tremolo.cir)\n");
    {
        Tremolo trem;
        const double sr = 96000.0;
        trem.prepare(sr);
        const auto& dc = trem.oscillator().dcOperatingPoint();
        near("DC op: out (melange 4.366)", dc[TwinTOscillator::OUT_NODE], 4.366, 0.05);
        near("DC op: base3 (melange 1.236)", dc[TwinTOscillator::BASE3], 1.236, 0.02);
        near("DC op: LED anode (melange 13.414)", dc[TwinTOscillator::LEDA], 13.414, 0.05);

        // Run 4 s, measure LED current range and oscillation rate via upward mean crossings
        const int n = static_cast<int>(sr * 4.0);
        std::vector<double> led(n);
        double lo = 1e9, hi = -1e9, mean = 0.0;
        for (int i = 0; i < n; ++i) { trem.process(); led[i] = trem.ledCurrentMa(); mean += led[i]; }
        mean /= n;
        int crossings = 0; double first = -1, last = -1;
        for (int i = 1; i < n; ++i) {
            lo = std::min(lo, led[i]); hi = std::max(hi, led[i]);
            if (led[i - 1] < mean && led[i] >= mean) { if (first < 0) first = i; last = i; ++crossings; }
        }
        const double rate = crossings > 1 ? (crossings - 1) * sr / (last - first) : 0.0;
        check(rate > 5.4 && rate < 6.0, "oscillation rate (Hz, deck: ~5.5-5.9)", rate, 5.65, 0.3);
        near("LED current min (mA, upstream 0.365)", lo, 0.365, 0.08);
        near("LED current max (mA, upstream 2.279)", hi, 2.279, 0.25);
    }

    std::printf("[6] Reed voice sanity\n");
    {
        ReedExciter r;
        ElectrostaticPickup pk;
        const double sr = 48000.0;
        r.noteOn(60, 0.8, sr, 12345u, ReedVoicing{});
        pk.init(sr, 60, r.swingMm());
        double peak = 0.0;
        for (int i = 0; i < static_cast<int>(sr); ++i) peak = std::max(peak, std::fabs(pk.process(r.next()) * r.outputScale()));
        check(peak > 1e-4 && peak < 0.5, "C4 v0.8 pickup peak (V, millivolt-scale)", peak, 0.01, 0.0);
    }

    std::printf(gFailures ? "\n%d CHECK(S) FAILED\n" : "\nALL OPENWURLI PORT CHECKS PASSED\n", gFailures);
    return gFailures ? 1 : 0;
}
