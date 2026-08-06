#pragma once

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Shared loader for this repo's golden-vector CSVs.
//
// Every generator writes the same shape: a header row, then per-case rows whose
// non-{kind,idx,val} columns repeat that case's integer parameters, with
// kind=raw holding input samples and kind=rgb holding the expected packed
// output. Only the parameter columns differ between arms (dfxisp_accel has
// mode/threshold/sel_*, default_isp has awb_mode, lowlight_isp has
// denoise/bin_mode), so the parser is driven by the header rather than fixed
// column indices -- which is why one loader is shorter than any of the three
// bespoke parsers it replaces.
//
// Required columns (all arms): case, in_w, in_h, out_w, out_h, kind, idx, val.

struct GoldenCase {
    std::string name;
    std::vector<std::string> keys;   // parameter column names, in header order
    std::vector<int> vals;           // matching values for this case
    std::vector<uint16_t> raw;       // kind=raw, sized in_w * in_h
    std::vector<uint32_t> expected;  // kind=rgb, sized out_w * out_h

    int param(const char* key) const {
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == key) return vals[i];
        }
        std::cerr << "golden CSV: case " << name << " has no column '" << key << "'\n";
        std::abort();
    }
};

inline std::vector<std::string> golden_csv_split(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) out.push_back(cell);
    return out;
}

// Returns an empty vector when the file is absent, so callers can skip the
// golden comparison the way they did with their own parsers.
inline std::vector<GoldenCase> load_golden_csv(const char* path) {
    std::vector<GoldenCase> cases;
    std::ifstream f(path);
    if (!f) return cases;

    std::string line;
    if (!std::getline(f, line)) return cases;
    const std::vector<std::string> header = golden_csv_split(line);

    while (std::getline(f, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> col = golden_csv_split(line);
        if (col.size() != header.size()) {
            std::cerr << "golden CSV: row has " << col.size() << " columns, header has "
                      << header.size() << "\n";
            std::abort();
        }

        std::string name;
        std::string kind;
        int idx = 0;
        std::string val;
        std::vector<std::string> keys;
        std::vector<int> vals;
        for (std::size_t i = 0; i < header.size(); ++i) {
            const std::string& h = header[i];
            if (h == "case") name = col[i];
            else if (h == "kind") kind = col[i];
            else if (h == "idx") idx = std::stoi(col[i]);
            else if (h == "val") val = col[i];
            else {
                keys.push_back(h);
                vals.push_back(std::stoi(col[i]));
            }
        }

        if (cases.empty() || cases.back().name != name) {
            GoldenCase c;
            c.name = name;
            c.keys = keys;
            c.vals = vals;
            c.raw.assign(std::size_t(c.param("in_w")) * std::size_t(c.param("in_h")), 0);
            c.expected.assign(std::size_t(c.param("out_w")) * std::size_t(c.param("out_h")), 0);
            cases.push_back(c);
        }
        GoldenCase& c = cases.back();
        if (kind == "raw") {
            if (idx < 0 || std::size_t(idx) >= c.raw.size()) {
                std::cerr << "golden CSV: raw idx " << idx << " out of range in " << name << "\n";
                std::abort();
            }
            c.raw[idx] = static_cast<uint16_t>(std::stoul(val));
        } else {
            if (idx < 0 || std::size_t(idx) >= c.expected.size()) {
                std::cerr << "golden CSV: rgb idx " << idx << " out of range in " << name << "\n";
                std::abort();
            }
            c.expected[idx] = static_cast<uint32_t>(std::stoul(val, nullptr, 0));
        }
    }
    return cases;
}
