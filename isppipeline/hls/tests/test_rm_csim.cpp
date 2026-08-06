#include "dfxisp_rm.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// Compares dfxisp_accel_variant against bit-exact golden vectors produced by
// tools/gen_rm_golden.py. CSV: case,variant,width,height,index,raw,expected_rgb_hex

struct Key {
    std::string name; int variant;
    bool operator<(const Key& o) const { return name < o.name || (name == o.name && variant < o.variant); }
};
struct Case { int w = 0, h = 0; std::vector<uint16_t> raw; std::vector<uint32_t> exp; };

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "tests/rm_golden_vectors.csv";
    std::ifstream f(path);
    if (!f) { std::cerr << "cannot open " << path << "\n"; return 2; }
    std::string line; std::getline(f, line);  // header
    std::map<Key, Case> cases;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line); std::vector<std::string> c; std::string cell;
        while (std::getline(ss, cell, ',')) c.push_back(cell);
        if (c.size() != 7) { std::cerr << "bad row cols=" << c.size() << "\n"; return 2; }
        Key k{c[0], std::stoi(c[1])};
        int w = std::stoi(c[2]), h = std::stoi(c[3]), idx = std::stoi(c[4]);
        Case& cs = cases[k];
        if (cs.w == 0) { cs.w = w; cs.h = h; cs.raw.assign(w * h, 0); cs.exp.assign(w * h, 0); }
        cs.raw[idx] = static_cast<uint16_t>(std::stoul(c[5]));
        cs.exp[idx] = static_cast<uint32_t>(std::stoul(c[6], nullptr, 0));
    }

    long checked = 0, mismatch = 0;
    for (auto& kv : cases) {
        const Key& k = kv.first; Case& cs = kv.second;
        std::vector<uint32_t> got(cs.w * cs.h, 0);
        dfxisp_accel_variant(cs.raw.data(), got.data(), cs.w, cs.h, k.variant);
        for (int i = 0; i < cs.w * cs.h; ++i) {
            ++checked;
            if (got[i] != cs.exp[i]) {
                if (mismatch < 5)
                    std::cerr << "mismatch case=" << k.name << " variant=" << k.variant
                              << " idx=" << i << " exp=0x" << std::hex << cs.exp[i]
                              << " got=0x" << got[i] << std::dec << "\n";
                ++mismatch;
            }
        }
    }
    std::cout << "DFXISP RM golden compare: checked=" << checked
              << " mismatch=" << mismatch << " cases=" << cases.size() << "\n";
    if (mismatch) return 1;
    std::cout << "DFXISP RM variant C-sim passed\n";
    return 0;
}
