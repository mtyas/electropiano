#pragma once

// The tunings the editor offers: equal temperament, a few built-in historical and microtonal
// scales, and every Scala file (.scl, with a .kbm of the same name if present) in the user's
// Tunings folder, next to the user presets.

#include "PhsPresetManager.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

namespace phs {

struct TuningEntry {
    std::string name;
    std::string scl;   // empty = 12-TET
    std::string kbm;
    std::string path;  // file it came from (empty for built-ins)
};

inline std::string userDataDirectory() {
    return std::filesystem::path(PhsPresetManager::getUserPresetsDirectory()).parent_path().string();
}
inline std::string userTuningsDirectory() {
    return (std::filesystem::path(userDataDirectory()) / "Tunings").string();
}

inline std::string equalDivisionScl(int n, const char* name) {
    std::ostringstream o;
    o << "! " << name << "\n" << name << "\n " << n << "\n";
    for (int k = 1; k <= n; ++k) o << std::to_string(1200.0 * k / n) << "\n";
    return o.str();
}

// A 12-note scale from its cents above C (C itself left out)
inline std::string twelveNoteScl(const char* name, std::initializer_list<double> cents) {
    std::ostringstream o;
    o << "! " << name << "\n" << name << "\n 12\n";
    for (double c : cents) o << std::to_string(c) << "\n";
    o << "1200.0\n";
    return o.str();
}

// A regular temperament: the chain of fifths of the given size from Eb to G#
inline std::string fifthsScl(const char* name, double fifth) {
    double pc[12] = {};
    for (int k = -3; k <= 8; ++k) {
        double c = std::fmod(k * fifth, 1200.0);
        if (c < 0.0) c += 1200.0;
        pc[((k * 7) % 12 + 12) % 12] = c;
    }
    std::ostringstream o;
    o << "! " << name << "\n" << name << "\n 12\n";
    for (int k = 1; k < 12; ++k) o << std::to_string(pc[k]) << "\n";
    o << "1200.0\n";
    return o.str();
}

// n equal divisions of an interval of `period` cents (an octave unless given)
inline std::string equalDivisionPeriod(int n, double period, const char* name) {
    std::ostringstream o;
    o << "! " << name << "\n" << name << "\n " << n << "\n";
    for (int k = 1; k <= n; ++k) o << std::to_string(period * k / n) << "\n";
    return o.str();
}

inline std::vector<TuningEntry> builtInTunings() {
    return {
        {"12-TET (equal temperament)", "", "", ""},
        {"Just intonation (5-limit, C)",
         "! just.scl\n5-limit just intonation\n 12\n16/15\n9/8\n6/5\n5/4\n4/3\n45/32\n3/2\n8/5\n5/3\n9/5\n15/8\n2/1\n", "", ""},
        {"Pythagorean (C)",
         "! pyth.scl\nPythagorean\n 12\n256/243\n9/8\n32/27\n81/64\n4/3\n729/512\n3/2\n128/81\n27/16\n16/9\n243/128\n2/1\n", "", ""},
        {"Quarter-comma meantone (C)",
         "! meantone.scl\n1/4-comma meantone\n 12\n76.049\n193.157\n310.265\n386.314\n503.422\n579.471\n696.578\n772.627\n889.735\n1006.843\n1082.892\n1200.0\n", "", ""},
        {"Werckmeister III",
         "! werck3.scl\nWerckmeister III\n 12\n90.225\n192.180\n294.135\n390.225\n498.045\n588.270\n696.090\n792.180\n888.270\n996.090\n1092.180\n1200.0\n", "", ""},
        {"19-EDO", equalDivisionScl(19, "19-tone equal division of the octave"), "", ""},
        {"24-EDO (quarter tones)", equalDivisionScl(24, "24-tone equal division of the octave"), "", ""},
        {"31-EDO", equalDivisionScl(31, "31-tone equal division of the octave"), "", ""},
        // Historical well temperaments and meantones (C)
        {"Kirnberger III", twelveNoteScl("Kirnberger III", {90.225, 193.157, 294.135, 386.314, 498.045, 590.224, 696.578, 792.180, 889.735, 996.090, 1088.269}), "", ""},
        {"Vallotti", twelveNoteScl("Vallotti", {94.135, 196.090, 298.045, 392.180, 501.955, 592.180, 698.045, 796.090, 894.135, 1000.0, 1090.225}), "", ""},
        {"Young II", twelveNoteScl("Thomas Young (1799)", {90.225, 196.090, 294.135, 392.180, 498.045, 588.270, 698.045, 792.180, 894.135, 996.090, 1090.225}), "", ""},
        {"Sixth-comma meantone (C)", fifthsScl("1/6-comma meantone", 698.371), "", ""},
        {"Third-comma meantone (C)", fifthsScl("1/3-comma meantone", 694.786), "", ""},
        // Just and harmonic
        {"Just intonation (7-limit, C)",
         "! just7.scl\n7-limit just intonation\n 12\n16/15\n9/8\n7/6\n5/4\n4/3\n7/5\n3/2\n8/5\n5/3\n7/4\n15/8\n2/1\n", "", ""},
        {"Harmonic series (partials 16-31)",
         "! harmonic.scl\nOvertone scale\n 12\n17/16\n9/8\n19/16\n5/4\n21/16\n11/8\n3/2\n13/8\n27/16\n7/4\n15/8\n2/1\n", "", ""},
        // World scales mapped onto the twelve keys
        {"Maqam Rast (C, neutral E and B)", twelveNoteScl("Maqam Rast", {100.0, 200.0, 300.0, 350.0, 500.0, 600.0, 700.0, 800.0, 900.0, 1000.0, 1050.0}), "", ""},
        {"Maqam Bayati (D, neutral E)", twelveNoteScl("Maqam Bayati", {100.0, 200.0, 300.0, 350.0, 500.0, 600.0, 700.0, 800.0, 900.0, 1000.0, 1100.0}), "", ""},
        {"Slendro (5 tones, approx.)", equalDivisionScl(5, "Slendro, approximated as 5 equal steps"), "", ""},
        {"Pelog (7 tones, approx.)",
         "! pelog.scl\nPelog, a common approximation\n 7\n120.0\n270.0\n540.0\n670.0\n785.0\n950.0\n1200.0\n", "", ""},
        // Other equal divisions
        {"17-EDO", equalDivisionScl(17, "17-tone equal division of the octave"), "", ""},
        {"22-EDO", equalDivisionScl(22, "22-tone equal division of the octave"), "", ""},
        {"53-EDO", equalDivisionScl(53, "53-tone equal division of the octave"), "", ""},
        {"Bohlen-Pierce (13 steps of 3/1)", equalDivisionPeriod(13, 1901.955, "Bohlen-Pierce, equal-tempered"), "", ""},
        {"Wendy Carlos Alpha (78 c steps)", equalDivisionPeriod(1, 77.965, "Carlos Alpha"), "", ""},
    };
}

inline bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return out.size() < (1u << 20);
}

// A Scala file and, if one sits beside it with the same name, its keyboard mapping
inline bool loadTuningFile(const std::string& sclPath, TuningEntry& out) {
    out = {};
    if (!readTextFile(sclPath, out.scl)) return false;
    std::filesystem::path kbm = sclPath;
    kbm.replace_extension(".kbm");
    std::error_code ec;
    if (std::filesystem::exists(kbm, ec)) readTextFile(kbm.string(), out.kbm);
    out.name = std::filesystem::path(sclPath).stem().string();
    out.path = sclPath;
    return true;
}

// Built-ins followed by the user's .scl files (sorted by name)
inline std::vector<TuningEntry> availableTunings() {
    std::vector<TuningEntry> list = builtInTunings();
    std::vector<TuningEntry> user;
    try {
        const std::string dir = userTuningsDirectory();
        std::filesystem::create_directories(dir);
        for (const auto& f : std::filesystem::directory_iterator(dir)) {
            std::string ext = f.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            TuningEntry e;
            if (ext == ".scl" && loadTuningFile(f.path().string(), e)) user.push_back(std::move(e));
        }
    } catch (...) {
    }
    std::sort(user.begin(), user.end(), [](const TuningEntry& a, const TuningEntry& b) { return a.name < b.name; });
    list.insert(list.end(), user.begin(), user.end());
    return list;
}

// Applies an entry to a tuning table (12-TET when it has no scale)
inline bool applyTuning(TuningTable& t, const TuningEntry& e, std::string* error = nullptr) {
    if (e.scl.empty()) { t.resetToEqual(); return true; }
    return t.load(e.scl, e.kbm, e.name, error);
}

} // namespace phs
