#pragma once

// Microtuning: Scala scales (.scl) with optional keyboard mappings (.kbm), and the MIDI Tuning
// Standard (real-time single-note and scale/octave messages).
//
// The result is one table: for every MIDI key, its offset in cents from 12-tone equal
// temperament at A4 = 440 Hz (the Master Tune knob then moves the whole instrument), or
// "unmapped" for keys a .kbm leaves out, which do not sound. The audio thread reads it
// lock-free; scales are parsed on the UI / host thread.
//
// Scala file format: https://www.huygens-fokker.org/scala/scl_format.html
// Keyboard mapping:  https://www.huygens-fokker.org/scala/help.htm#mappings

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace phs {

// Marks a key the keyboard mapping leaves out (a sentinel, not NaN: the DSP builds with fast-math)
static constexpr float kUnmappedKey = 1.0e9f;
inline bool isUnmapped(float cents) { return cents > 1.0e8f; }

struct ScalaScale {
    std::string description;
    std::vector<double> cents; // degrees 1..N above the tonic; the last one is the period (usually 1200)
};

struct KeyboardMapping {
    int size = 0;              // 0 = linear mapping, one key per scale degree
    int firstKey = 0, lastKey = 127;
    int middleKey = 60;        // key where scale degree 0 sits
    int referenceKey = 69;     // key tuned to referenceHz
    double referenceHz = 440.0;
    int octaveDegree = 0;      // scale degree that forms the mapping's period (0 = the scale's own)
    std::vector<int> map;      // per key in the pattern: scale degree, or -1 for an unmapped key
};

namespace tuning_detail {
// Splits into lines, dropping '!' comments and trailing '\r'
inline std::vector<std::string> contentLines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == '!') continue;
        out.push_back(line);
    }
    return out;
}
inline std::string firstToken(const std::string& line) {
    size_t a = line.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    size_t b = line.find_first_of(" \t", a);
    return line.substr(a, b == std::string::npos ? std::string::npos : b - a);
}
} // namespace tuning_detail

// Parses a .scl file. Pitch lines are cents when they contain a '.', otherwise a ratio "a/b" or "a".
inline bool parseScala(const std::string& text, ScalaScale& out, std::string* error = nullptr) {
    using namespace tuning_detail;
    auto fail = [&](const char* why) { if (error) *error = why; return false; };
    const auto lines = contentLines(text);
    if (lines.size() < 2) return fail("not a Scala file");
    out = {};
    out.description = lines[0];
    const std::string countTok = firstToken(lines[1]);
    if (countTok.empty()) return fail("missing note count");
    const long count = std::strtol(countTok.c_str(), nullptr, 10);
    if (count < 1 || count > 1024) return fail("note count out of range");
    for (size_t i = 2; i < lines.size() && static_cast<long>(out.cents.size()) < count; ++i) {
        const std::string tok = firstToken(lines[i]);
        if (tok.empty()) continue;
        double c;
        if (tok.find('.') != std::string::npos) {
            char* end = nullptr;
            c = std::strtod(tok.c_str(), &end);
            if (end == tok.c_str()) return fail("bad cents value");
        } else {
            const size_t slash = tok.find('/');
            const double num = std::strtod(tok.substr(0, slash).c_str(), nullptr);
            const double den = slash == std::string::npos ? 1.0 : std::strtod(tok.substr(slash + 1).c_str(), nullptr);
            if (!(num > 0.0) || !(den > 0.0)) return fail("bad ratio");
            c = 1200.0 * std::log2(num / den);
        }
        if (!std::isfinite(c)) return fail("bad pitch");
        out.cents.push_back(c);
    }
    if (static_cast<long>(out.cents.size()) != count) return fail("fewer pitches than the note count");
    if (!(out.cents.back() > 0.0)) return fail("the period must be above the tonic");
    return true;
}

// Parses a .kbm file
inline bool parseKeyboardMapping(const std::string& text, KeyboardMapping& out, std::string* error = nullptr) {
    using namespace tuning_detail;
    auto fail = [&](const char* why) { if (error) *error = why; return false; };
    std::vector<std::string> toks;
    for (const auto& l : contentLines(text)) {
        const std::string t = firstToken(l);
        if (!t.empty()) toks.push_back(t);
    }
    if (toks.size() < 7) return fail("not a keyboard mapping");
    out = {};
    out.size = std::atoi(toks[0].c_str());
    out.firstKey = std::atoi(toks[1].c_str());
    out.lastKey = std::atoi(toks[2].c_str());
    out.middleKey = std::atoi(toks[3].c_str());
    out.referenceKey = std::atoi(toks[4].c_str());
    out.referenceHz = std::strtod(toks[5].c_str(), nullptr);
    out.octaveDegree = std::atoi(toks[6].c_str());
    if (out.size < 0 || out.size > 128 || !(out.referenceHz > 0.0)) return fail("bad mapping header");
    for (int i = 0; i < out.size; ++i) {
        const size_t k = 7 + static_cast<size_t>(i);
        if (k >= toks.size() || toks[k] == "x" || toks[k] == "X") out.map.push_back(-1);
        else out.map.push_back(std::atoi(toks[k].c_str()));
    }
    return true;
}

// Fills cents[128] (offset from 12-TET at A4 = 440) for a scale and mapping; kUnmappedKey = left out
inline void computeTuningTable(const ScalaScale& scale, const KeyboardMapping& kbm, float* centsOut) {
    const int n = static_cast<int>(scale.cents.size());
    const double period = scale.cents.back();
    // Cents of a scale degree above the tonic (any integer, folded through the period)
    auto degreeCents = [&](int degree) {
        const int oct = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
        const int d = degree - oct * n;
        return oct * period + (d == 0 ? 0.0 : scale.cents[d - 1]);
    };
    const bool linear = kbm.size == 0;
    const int mapPeriodDegrees = linear ? 1 : (kbm.octaveDegree > 0 ? kbm.octaveDegree : n);
    // Scale degree of a key, or INT32_MIN when the mapping leaves it out
    auto keyDegree = [&](int key) {
        if (key < kbm.firstKey || key > kbm.lastKey) return INT32_MIN;
        const int rel = key - kbm.middleKey;
        if (linear) return rel;
        const int rep = rel >= 0 ? rel / kbm.size : -((-rel + kbm.size - 1) / kbm.size);
        const int idx = rel - rep * kbm.size;
        const int deg = kbm.map[static_cast<size_t>(idx)];
        return deg < 0 ? INT32_MIN : rep * mapPeriodDegrees + deg;
    };
    // The reference key sounds at referenceHz even if it is itself unmapped: measure from its
    // degree when mapped, otherwise from the linear position
    const int refDeg = keyDegree(kbm.referenceKey);
    const double refCents = refDeg == INT32_MIN ? (kbm.referenceKey - kbm.middleKey) * 100.0 : degreeCents(refDeg);
    const double refOffset = 1200.0 * std::log2(kbm.referenceHz / 440.0) - (kbm.referenceKey - 69) * 100.0;
    for (int key = 0; key < 128; ++key) {
        const int deg = keyDegree(key);
        if (deg == INT32_MIN) { centsOut[key] = kUnmappedKey; continue; }
        const double pitch = degreeCents(deg) - refCents;               // cents above the reference key
        centsOut[key] = static_cast<float>(pitch - (key - kbm.referenceKey) * 100.0 + refOffset);
    }
}

class TuningTable {
public:
    TuningTable() { resetToEqual(); }

    // ---- Audio thread ---------------------------------------------------------
    // Offset in cents from 12-TET for a key; returns false for a key the mapping leaves unmapped
    bool centsFor(int key, float& cents) const {
        const float c = cents_[static_cast<size_t>(key & 127)].load(std::memory_order_relaxed);
        if (isUnmapped(c)) return false;
        cents = c;
        return true;
    }

    // MIDI Tuning Standard SysEx (F0 ... F7). Handles real-time / non-real-time single-note tuning
    // change (08 02 / 08 07) and scale/octave tuning, 1-byte and 2-byte form (08 08 / 08 09).
    // Returns true if the message was a tuning message.
    bool handleSysEx(const uint8_t* d, size_t size) {
        if (size < 6 || d[0] != 0xF0) return false;
        if ((d[1] != 0x7F && d[1] != 0x7E) || d[3] != 0x08) return false;
        const uint8_t sub = d[4];
        if (sub == 0x02 || sub == 0x07) {
            // [F0 7F dev 08 02 prog count {key xx yy zz}... F7], 08 07 adds a bank byte first
            size_t i = sub == 0x02 ? 6 : 7;
            if (i >= size) return true;
            const int count = d[i++];
            for (int k = 0; k < count && i + 3 < size; ++k, i += 4) {
                const int key = d[i] & 0x7F, semi = d[i + 1] & 0x7F;
                const int frac = ((d[i + 2] & 0x7F) << 7) | (d[i + 3] & 0x7F);
                if (semi == 0x7F && frac == 0x3FFF) continue; // "no change"
                const double cents = (semi - key) * 100.0 + frac * (100.0 / 16384.0);
                cents_[static_cast<size_t>(key)].store(static_cast<float>(cents), std::memory_order_relaxed);
            }
            markCustom("MIDI Tuning Standard");
            return true;
        }
        if (sub == 0x08 || sub == 0x09) {
            // [F0 7x dev 08 08 ff ff ff c0..c11 F7]: per pitch class, -64..+63 cents
            // 08 09: two bytes per pitch class, 14 bits over -100..+100 cents
            const bool twoByte = sub == 0x09;
            const size_t need = 8 + (twoByte ? 24 : 12);
            if (size < need) return true;
            float pc[12];
            for (int k = 0; k < 12; ++k) {
                if (twoByte) {
                    const int v = ((d[8 + 2 * k] & 0x7F) << 7) | (d[9 + 2 * k] & 0x7F);
                    pc[k] = static_cast<float>((v - 8192) * (100.0 / 8192.0));
                } else {
                    pc[k] = static_cast<float>((d[8 + k] & 0x7F) - 64);
                }
            }
            for (int key = 0; key < 128; ++key) cents_[static_cast<size_t>(key)].store(pc[key % 12], std::memory_order_relaxed);
            markCustom("MIDI Tuning Standard");
            return true;
        }
        return false;
    }

    // ---- UI / host thread -----------------------------------------------------
    void resetToEqual() {
        std::lock_guard<std::mutex> lock(textMutex_);
        for (auto& c : cents_) c.store(0.0f, std::memory_order_relaxed);
        name_ = "12-TET";
        sclText_.clear();
        kbmText_.clear();
        generation_.fetch_add(1, std::memory_order_relaxed);
    }

    // Loads a scale (and optionally a mapping); the previous tuning stays on failure
    bool load(const std::string& sclText, const std::string& kbmText, const std::string& name,
              std::string* error = nullptr) {
        ScalaScale scale;
        if (!parseScala(sclText, scale, error)) return false;
        KeyboardMapping kbm;
        if (!kbmText.empty() && !parseKeyboardMapping(kbmText, kbm, error)) return false;
        float table[128];
        computeTuningTable(scale, kbm, table);
        std::lock_guard<std::mutex> lock(textMutex_);
        for (int k = 0; k < 128; ++k) cents_[static_cast<size_t>(k)].store(table[k], std::memory_order_relaxed);
        sclText_ = sclText;
        kbmText_ = kbmText;
        name_ = name.empty() ? scale.description : name;
        generation_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool isEqualTemperament() const {
        std::lock_guard<std::mutex> lock(textMutex_);
        return sclText_.empty() && name_ == "12-TET";
    }
    std::string name() const { std::lock_guard<std::mutex> lock(textMutex_); return name_; }
    std::string sclText() const { std::lock_guard<std::mutex> lock(textMutex_); return sclText_; }
    std::string kbmText() const { std::lock_guard<std::mutex> lock(textMutex_); return kbmText_; }
    // Changes whenever the tuning does (the editor redraws its read-out on it)
    uint32_t generation() const { return generation_.load(std::memory_order_relaxed); }

    // Serialised form for the plugin state: name, .scl and .kbm texts. A tuning that came from
    // MIDI Tuning Standard messages is stored as its per-key table.
    std::string serialize() {
        syncAfterSysEx();
        std::lock_guard<std::mutex> lock(textMutex_);
        std::ostringstream o;
        o << "PHSTUNE1\n" << name_.size() << "\n" << name_ << sclText_.size() << "\n" << sclText_
          << kbmText_.size() << "\n" << kbmText_;
        if (sclText_.empty()) {
            o << "128\n";
            for (const auto& c : cents_) {
                const float v = c.load(std::memory_order_relaxed);
                o << (isUnmapped(v) ? std::string("x") : std::to_string(v)) << "\n";
            }
        } else {
            o << "0\n";
        }
        return o.str();
    }
    bool deserialize(const std::string& data) {
        std::istringstream in(data);
        std::string magic;
        if (!std::getline(in, magic) || magic != "PHSTUNE1") return false;
        auto readBlock = [&](std::string& out) {
            std::string len;
            if (!std::getline(in, len)) return false;
            const size_t n = static_cast<size_t>(std::strtoul(len.c_str(), nullptr, 10));
            if (n > (1u << 20)) return false;
            out.resize(n);
            return n == 0 || static_cast<bool>(in.read(&out[0], static_cast<std::streamsize>(n)));
        };
        std::string name, scl, kbm;
        if (!readBlock(name) || !readBlock(scl) || !readBlock(kbm)) return false;
        if (!scl.empty()) return load(scl, kbm, name);
        std::string countLine;
        std::getline(in, countLine);
        if (std::atoi(countLine.c_str()) != 128) { resetToEqual(); return true; }
        std::lock_guard<std::mutex> lock(textMutex_);
        for (auto& c : cents_) {
            std::string v;
            std::getline(in, v);
            c.store(v == "x" ? kUnmappedKey : std::strtof(v.c_str(), nullptr), std::memory_order_relaxed);
        }
        name_ = name;
        sclText_.clear();
        kbmText_.clear();
        generation_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

private:
    void markCustom(const char* name) {
        // Audio thread: only the generation is bumped; the name is fixed up by the next UI read
        customPending_.store(true, std::memory_order_relaxed);
        (void)name;
        generation_.fetch_add(1, std::memory_order_relaxed);
    }

public:
    // UI thread: adopt a MIDI Tuning Standard change as the named tuning (drops the .scl text)
    void syncAfterSysEx() {
        if (!customPending_.exchange(false, std::memory_order_relaxed)) return;
        std::lock_guard<std::mutex> lock(textMutex_);
        name_ = "MIDI Tuning Standard";
        sclText_.clear();
        kbmText_.clear();
    }

private:
    std::array<std::atomic<float>, 128> cents_{};
    std::atomic<uint32_t> generation_{0};
    std::atomic<bool> customPending_{false};
    mutable std::mutex textMutex_; // never taken on the audio thread
    std::string name_, sclText_, kbmText_;
};

} // namespace phs
