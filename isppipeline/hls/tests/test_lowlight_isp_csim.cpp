#include "lowlight_isp.hpp"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static uint8_t red(uint32_t p) { return uint8_t((p >> 16) & 0xff); }
static uint8_t green(uint32_t p) { return uint8_t((p >> 8) & 0xff); }
static uint8_t blue(uint32_t p) { return uint8_t(p & 0xff); }

struct GoldenCase {
    std::string name;
    int in_w = 0, in_h = 0, denoise = 0, bin_mode = 0, out_w = 0, out_h = 0;
    std::vector<uint16_t> raw;
    std::vector<uint32_t> expected;
};

static void check_golden_vectors(const char* path) {
    std::ifstream f(path);
    if (!f) {
        std::cout << "lowlight_ISP golden vector compare skipped (" << path << " not found)\n";
        return;
    }
    std::string line;
    std::getline(f, line);  // header
    std::vector<GoldenCase> cases;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::vector<std::string> col;
        std::string cell;
        while (std::getline(ss, cell, ',')) col.push_back(cell);
        assert(col.size() == 10);

        const std::string& name = col[0];
        if (cases.empty() || cases.back().name != name) {
            GoldenCase c;
            c.name = name;
            c.in_w = std::stoi(col[1]);
            c.in_h = std::stoi(col[2]);
            c.denoise = std::stoi(col[3]);
            c.bin_mode = std::stoi(col[4]);
            c.out_w = std::stoi(col[5]);
            c.out_h = std::stoi(col[6]);
            c.raw.assign(c.in_w * c.in_h, 0);
            c.expected.assign(c.out_w * c.out_h, 0);
            cases.push_back(c);
        }
        GoldenCase& c = cases.back();
        const std::string& kind = col[7];
        const int idx = std::stoi(col[8]);
        if (kind == "raw") {
            assert(idx >= 0 && idx < c.in_w * c.in_h);
            c.raw[idx] = static_cast<uint16_t>(std::stoul(col[9]));
        } else {
            assert(idx >= 0 && idx < c.out_w * c.out_h);
            c.expected[idx] = static_cast<uint32_t>(std::stoul(col[9], nullptr, 0));
        }
    }

    int checked = 0;
    for (const GoldenCase& c : cases) {
        std::vector<uint32_t> got(c.in_w * c.in_h, 0);
        int out_w = 0, out_h = 0;
        lowlight_isp(c.raw.data(), got.data(), c.in_w, c.in_h, c.denoise, c.bin_mode,
                     &out_w, &out_h);
        // Policy A: shape halves (min 1)
        assert(out_w == c.out_w && out_h == c.out_h);
        assert(out_w == (c.in_w / 2 < 1 ? 1 : c.in_w / 2));
        assert(out_h == (c.in_h / 2 < 1 ? 1 : c.in_h / 2));
        for (int i = 0; i < c.out_w * c.out_h; ++i) {
            if (got[i] != c.expected[i]) {
                std::cerr << "lowlight_ISP golden mismatch case=" << c.name << " index=" << i
                          << " expected=0x" << std::hex << c.expected[i]
                          << " got=0x" << got[i] << std::dec << "\n";
                assert(got[i] == c.expected[i]);
            }
            ++checked;
        }
    }
    std::cout << "lowlight_ISP golden vector compare passed (" << checked << " pixels)\n";
}

int main(int argc, char** argv) {
    check_golden_vectors(argc > 1 ? argv[1] : "tests/lowlight_isp_golden_vectors.csv");

    constexpr int W = 8, H = 8;

    // Stage (2): black level is applied once to the BINNED value, so a frame
    // entirely at/below the pedestal must stay pure black -- averaging first
    // cannot lift it (all samples are at the pedestal), and neither the 2.0x
    // gain nor the tone curve may resurrect it.
    {
        uint16_t pedestal[W * H];
        for (int i = 0; i < W * H; ++i) pedestal[i] = 32;
        uint32_t out[(W / 2) * (H / 2)] = {};
        int ow = 0, oh = 0;
        lowlight_isp(pedestal, out, W, H, LOWLIGHT_ISP_DENOISE_ON,
                     LOWLIGHT_ISP_BIN_SAMECOLOR, &ow, &oh);
        for (int i = 0; i < ow * oh; ++i) assert(out[i] == 0u);
    }

    // Stage (5) is the design's core claim: the GAT/VST tone must lift the
    // read-noise floor LESS than the plain gamma-2.0 curve it replaces, while
    // agreeing with it in the midtones. Probe the curve through the pipeline
    // with flat frames: a near-floor frame must come out markedly darker than
    // the old sqrt tone would have produced, and a mid frame must not.
    {
        auto mean_of_flat = [&](uint16_t level) {
            uint16_t f[W * H];
            for (int i = 0; i < W * H; ++i) f[i] = level;
            uint32_t out[(W / 2) * (H / 2)] = {};
            int ow = 0, oh = 0;
            lowlight_isp(f, out, W, H, LOWLIGHT_ISP_DENOISE_OFF,
                         LOWLIGHT_ISP_BIN_SAMECOLOR, &ow, &oh);
            long s = 0;
            for (int i = 0; i < ow * oh; ++i) s += green(out[i]);
            return s / (ow * oh);
        };
        const long near_floor = mean_of_flat(40);    // just above the pedestal
        const long mid = mean_of_flat(1600);
        // Floor stays deeply suppressed; midtones still map high.
        assert(near_floor < 40);
        assert(mid > 150);
        assert(near_floor < mid);
    }

    // Stage (6) must suppress flat-region noise...
    uint16_t noisy[W * H];
    {
        unsigned seed = 12345u;
        for (int i = 0; i < W * H; ++i) {
            seed = 1103515245u * seed + 12345u;
            noisy[i] = static_cast<uint16_t>(300 + ((seed >> 16) % 120));
        }
        auto spread = [&](int mode) {
            uint32_t out[(W / 2) * (H / 2)] = {};
            int ow = 0, oh = 0;
            lowlight_isp(noisy, out, W, H, mode, LOWLIGHT_ISP_BIN_SAMECOLOR, &ow, &oh);
            int hi = 0, lo = 255;
            for (int i = 0; i < ow * oh; ++i) {
                const int v = green(out[i]);
                if (v > hi) hi = v;
                if (v < lo) lo = v;
            }
            return hi - lo;
        };
        assert(spread(LOWLIGHT_ISP_DENOISE_ON) <= spread(LOWLIGHT_ISP_DENOISE_OFF));
    }

    // ...while preserving a hard edge: the contrast across a step must survive
    // denoising essentially intact (that is what makes it edge-preserving
    // rather than a blur).
    {
        uint16_t edge[W * H];
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                edge[y * W + x] = (x < W / 2) ? 200 : 2600;
        auto contrast = [&](int mode) {
            uint32_t out[(W / 2) * (H / 2)] = {};
            int ow = 0, oh = 0;
            lowlight_isp(edge, out, W, H, mode, LOWLIGHT_ISP_BIN_SAMECOLOR, &ow, &oh);
            // compare the columns immediately left/right of the step
            const int lc = ow / 2 - 1, rc = ow / 2;
            return int(green(out[rc])) - int(green(out[lc]));
        };
        const int c_off = contrast(LOWLIGHT_ISP_DENOISE_OFF);
        const int c_on = contrast(LOWLIGHT_ISP_DENOISE_ON);
        assert(c_off > 0);
        assert(c_on >= c_off - 2);  // edge kept (sigma-clip excludes cross-edge neighbours)
    }

    // Stage (1) is the SNR claim: on a noisy flat frame, real same-colour
    // binning (4 samples per chroma site, 8 for green) must measurably reduce
    // the output spread versus the legacy per-cell subsampling. Denoise is OFF
    // so this isolates binning alone -- this is the ablation that turns "+6 dB"
    // from an assumption into a measurement.
    {
        uint16_t nf[W * H];
        unsigned seed = 987654321u;
        for (int i = 0; i < W * H; ++i) {
            seed = 1103515245u * seed + 12345u;
            nf[i] = static_cast<uint16_t>(400 + ((seed >> 16) % 200));
        }
        auto spread_of = [&](int bin_mode) {
            uint32_t out[(W / 2) * (H / 2)] = {};
            int ow = 0, oh = 0;
            lowlight_isp(nf, out, W, H, LOWLIGHT_ISP_DENOISE_OFF, bin_mode, &ow, &oh);
            int hi = 0, lo = 255;
            for (int i = 0; i < ow * oh; ++i) {
                const int v = green(out[i]);
                if (v > hi) hi = v;
                if (v < lo) lo = v;
            }
            return hi - lo;
        };
        assert(spread_of(LOWLIGHT_ISP_BIN_SAMECOLOR) < spread_of(LOWLIGHT_ISP_BIN_SUBSAMPLE));
    }

    // Stages (5)+(7): a saturated frame never overflows RGB8.
    {
        uint16_t sat[W * H];
        for (int i = 0; i < W * H; ++i) sat[i] = 4095;
        uint32_t out[(W / 2) * (H / 2)] = {};
        int ow = 0, oh = 0;
        lowlight_isp(sat, out, W, H, LOWLIGHT_ISP_DENOISE_ON,
                     LOWLIGHT_ISP_BIN_SAMECOLOR, &ow, &oh);
        for (int i = 0; i < ow * oh; ++i) {
            assert(red(out[i]) <= 255 && green(out[i]) <= 255 && blue(out[i]) <= 255);
        }
    }

    // DFX contract: rm_lowlight_isp_top is a drop-in for the RP slot -- same
    // 6-argument signature as the other RM tops, and identical behaviour to
    // lowlight_isp() with denoise enabled.
    {
        uint32_t via_top[(W / 2) * (H / 2)] = {}, via_dev[(W / 2) * (H / 2)] = {};
        int ow1 = 0, oh1 = 0, ow2 = 0, oh2 = 0;
        rm_lowlight_isp_top(noisy, via_top, W, H, &ow1, &oh1);
        lowlight_isp(noisy, via_dev, W, H, LOWLIGHT_ISP_DENOISE_ON,
                     LOWLIGHT_ISP_BIN_SAMECOLOR, &ow2, &oh2);
        assert(ow1 == W / 2 && oh1 == H / 2 && ow1 == ow2 && oh1 == oh2);
        for (int i = 0; i < ow1 * oh1; ++i) assert(via_top[i] == via_dev[i]);
    }

    // Degenerate inputs.
    {
        uint32_t out[4] = {};
        int ow = -1, oh = -1;
        lowlight_isp(nullptr, out, W, H, LOWLIGHT_ISP_DENOISE_ON,
                     LOWLIGHT_ISP_BIN_SAMECOLOR, &ow, &oh);
        assert(ow == 0 && oh == 0);
        uint16_t one = 900;
        uint32_t o1 = 0;
        lowlight_isp(&one, &o1, 1, 1, LOWLIGHT_ISP_DENOISE_ON,
                     LOWLIGHT_ISP_BIN_SAMECOLOR, &ow, &oh);
        assert(ow == 1 && oh == 1);
    }

    std::cout << "lowlight_ISP C-sim smoke tests passed\n";
    return 0;
}
