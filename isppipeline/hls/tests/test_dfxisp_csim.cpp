#include "dfxisp_accel.hpp"

#include <cassert>
#include <cstdint>
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
    int in_w = 0, in_h = 0, mode = 0;
    uint16_t threshold = 0;
    int out_w = 0, out_h = 0, sel_mode = 0, sel_rm = 0;
    std::vector<uint16_t> raw;
    std::vector<uint32_t> expected;
};

static void check_golden_vectors(const char* path) {
    std::ifstream f(path);
    if (!f) {
        std::cout << "DFXISP golden vector compare skipped (" << path << " not found)\n";
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
        assert(col.size() == 12);

        const std::string& name = col[0];
        if (cases.empty() || cases.back().name != name) {
            GoldenCase c;
            c.name = name;
            c.in_w = std::stoi(col[1]);
            c.in_h = std::stoi(col[2]);
            c.mode = std::stoi(col[3]);
            c.threshold = static_cast<uint16_t>(std::stoul(col[4]));
            c.out_w = std::stoi(col[5]);
            c.out_h = std::stoi(col[6]);
            c.sel_mode = std::stoi(col[7]);
            c.sel_rm = std::stoi(col[8]);
            c.raw.assign(c.in_w * c.in_h, 0);
            c.expected.assign(c.out_w * c.out_h, 0);
            cases.push_back(c);
        }
        GoldenCase& c = cases.back();
        const std::string& kind = col[9];
        const int idx = std::stoi(col[10]);
        if (kind == "raw") {
            assert(idx >= 0 && idx < c.in_w * c.in_h);
            c.raw[idx] = static_cast<uint16_t>(std::stoul(col[11]));
        } else {
            assert(idx >= 0 && idx < c.out_w * c.out_h);
            c.expected[idx] = static_cast<uint32_t>(std::stoul(col[11], nullptr, 0));
        }
    }

    int checked = 0;
    for (const GoldenCase& c : cases) {
        std::vector<uint32_t> got(c.in_w * c.in_h, 0);  // capacity >= out
        int out_w = 0, out_h = 0, sel_mode = 0, sel_rm = 0;
        dfxisp_accel(c.raw.data(), got.data(), c.in_w, c.in_h, c.mode, c.threshold,
                    &out_w, &out_h, &sel_mode, &sel_rm);

        // metadata gates (mode / selected RM / output shape)
        assert(sel_mode == c.sel_mode);
        assert(sel_rm == c.sel_rm);
        assert(out_w == c.out_w && out_h == c.out_h);
        // mutually exclusive tone RM: exactly one selected, consistent with mode
        assert(sel_rm == (sel_mode == DFXISP_MODE_LOW_LIGHT
                              ? DFXISP_RM_LOW_LIGHT_TONE : DFXISP_RM_NORMAL_TONE));
        // low-light is shape-changing (Policy A); normal preserves shape
        if (sel_mode == DFXISP_MODE_LOW_LIGHT) {
            assert(out_w == (c.in_w / 2 < 1 ? 1 : c.in_w / 2));
            assert(out_h == (c.in_h / 2 < 1 ? 1 : c.in_h / 2));
        } else {
            assert(out_w == c.in_w && out_h == c.in_h);
        }

        for (int i = 0; i < c.out_w * c.out_h; ++i) {
            if (got[i] != c.expected[i]) {
                std::cerr << "golden mismatch case=" << c.name << " index=" << i
                          << " expected=0x" << std::hex << c.expected[i]
                          << " got=0x" << got[i] << std::dec << "\n";
                assert(got[i] == c.expected[i]);
            }
            ++checked;
        }
    }
    std::cout << "DFXISP golden vector compare passed (" << checked << " pixels)\n";
}

int main() {
    constexpr int W = 8, H = 8;

    // Uniform mid-level frame: normal and low-light derive from the same baseline
    // value, isolating the tone RM effect (mutual exclusion + no double gain/gamma).
    uint16_t mid[W * H];
    for (int i = 0; i < W * H; ++i) mid[i] = 1600;

    uint32_t normal[W * H] = {};
    int ow_n = 0, oh_n = 0, sm_n = 0, sr_n = 0;
    dfxisp_accel(mid, normal, W, H, DFXISP_MODE_NORMAL, 512, &ow_n, &oh_n, &sm_n, &sr_n);
    // normal: RM_NORMAL_TONE, shape preserved, baseline output nonzero
    assert(sm_n == DFXISP_MODE_NORMAL);
    assert(sr_n == DFXISP_RM_NORMAL_TONE);
    assert(ow_n == W && oh_n == H);
    assert(normal[0] != 0);

    uint32_t low[W * H] = {};
    int ow_l = 0, oh_l = 0, sm_l = 0, sr_l = 0;
    dfxisp_accel(mid, low, W, H, DFXISP_MODE_LOW_LIGHT, 512, &ow_l, &oh_l, &sm_l, &sr_l);
    // low-light: low-light tone RM, shape halved (Policy A)
    assert(sm_l == DFXISP_MODE_LOW_LIGHT);
    assert(sr_l == DFXISP_RM_LOW_LIGHT_TONE);
    assert(ow_l == W / 2 && oh_l == H / 2);
    // low-light tone (gain 2.0x + gamma2.0) brightens vs normal tone (gain 1.25x + gamma2.0).
    assert(red(low[0]) > red(normal[0]));
    assert(green(low[0]) > green(normal[0]));
    assert(blue(low[0]) > blue(normal[0]));

    // AUTO on a dark frame selects the low-light tone RM.
    uint16_t dark[W * H];
    for (int i = 0; i < W * H; ++i) dark[i] = 200;
    uint32_t adark[W * H] = {};
    int ow_ad = 0, oh_ad = 0, sm_ad = 0, sr_ad = 0;
    dfxisp_accel(dark, adark, W, H, DFXISP_MODE_AUTO, 512, &ow_ad, &oh_ad, &sm_ad, &sr_ad);
    assert(sm_ad == DFXISP_MODE_LOW_LIGHT);
    assert(sr_ad == DFXISP_RM_LOW_LIGHT_TONE);

    // AUTO on a bright frame stays normal (RM_NORMAL_TONE = gain 1.25x + gamma2.0).
    uint16_t bright[W * H];
    for (int i = 0; i < W * H; ++i) bright[i] = 3000;
    uint32_t abright[W * H] = {};
    int ow_ab = 0, oh_ab = 0, sm_ab = 0, sr_ab = 0;
    dfxisp_accel(bright, abright, W, H, DFXISP_MODE_AUTO, 512, &ow_ab, &oh_ab, &sm_ab, &sr_ab);
    assert(sm_ab == DFXISP_MODE_NORMAL);
    assert(sr_ab == DFXISP_RM_NORMAL_TONE);

    // Checker C1 deployment (2026-07-20): DARK_RATIO_PCT 80 -> 62. On 64 pixels
    // the gate needs dark*100 > 62*64, i.e. dark >= 40: 39/64 (60.9%) must stay
    // NORMAL, 40/64 (62.5%) must trigger LOW_LIGHT (boundary regression).
    {
        uint16_t r61[W * H];
        int i = 0;
        for (; i < 39; ++i) r61[i] = 200;   // 39/64 = 60.9% dark
        for (; i < W * H; ++i) r61[i] = 3000;
        uint32_t out61[W * H] = {};
        int ow61 = 0, oh61 = 0, sm61 = 0, sr61 = 0;
        dfxisp_accel(r61, out61, W, H, DFXISP_MODE_AUTO, 512, &ow61, &oh61, &sm61, &sr61);
        assert(sm61 == DFXISP_MODE_NORMAL);   // 60.9% <= 62% -> NORMAL
        assert(sr61 == DFXISP_RM_NORMAL_TONE);

        uint16_t r62[W * H];
        i = 0;
        for (; i < 40; ++i) r62[i] = 200;    // 40/64 = 62.5% dark
        for (; i < W * H; ++i) r62[i] = 3000;
        uint32_t out62[W * H] = {};
        int ow62 = 0, oh62 = 0, sm62 = 0, sr62 = 0;
        dfxisp_accel(r62, out62, W, H, DFXISP_MODE_AUTO, 512, &ow62, &oh62, &sm62, &sr62);
        assert(sm62 == DFXISP_MODE_LOW_LIGHT);  // 62.5% > 62% -> LOW_LIGHT
        assert(sr62 == DFXISP_RM_LOW_LIGHT_TONE);
    }

    // RAW-domain boundary regression (adversarial review, 2026-07-04): the dark16
    // recommendation (checker-improvement-simulation-2026-07-03.md) is calibrated
    // on shift-8 pseudo-RAW where 8-bit 16 -> 16<<8 = 4096; on this 12-bit HLS
    // path the same threshold is 16<<4 = 256. A dataset-domain value (4096) here
    // would mark every valid pixel dark (raw < 4096 always) and route ALL frames
    // to LOW_LIGHT. Prove 256 does not: pixels at exactly 256 are NOT dark
    // (strict < compare) so AUTO stays NORMAL; one LSB lower flips to LOW_LIGHT.
    {
        uint16_t at_thr[W * H];
        for (int i = 0; i < W * H; ++i) at_thr[i] = 256;   // == threshold: not dark
        uint32_t out_at[W * H] = {};
        int ow_at = 0, oh_at = 0, sm_at = 0, sr_at = 0;
        dfxisp_accel(at_thr, out_at, W, H, DFXISP_MODE_AUTO, 256,
                    &ow_at, &oh_at, &sm_at, &sr_at);
        assert(sm_at == DFXISP_MODE_NORMAL);   // 0% dark -> NORMAL
        assert(sr_at == DFXISP_RM_NORMAL_TONE);

        uint16_t below_thr[W * H];
        for (int i = 0; i < W * H; ++i) below_thr[i] = 255;  // < threshold: dark
        uint32_t out_bt[W * H] = {};
        int ow_bt = 0, oh_bt = 0, sm_bt = 0, sr_bt = 0;
        dfxisp_accel(below_thr, out_bt, W, H, DFXISP_MODE_AUTO, 256,
                    &ow_bt, &oh_bt, &sm_bt, &sr_bt);
        assert(sm_bt == DFXISP_MODE_LOW_LIGHT);  // 100% dark -> LOW_LIGHT
        assert(sr_bt == DFXISP_RM_LOW_LIGHT_TONE);
    }

    // Saturation: gain + gamma2.0 tone never overflows RGB8.
    uint16_t sat[W * H];
    for (int i = 0; i < W * H; ++i) sat[i] = 4095;
    uint32_t sat_out[W * H] = {};
    int ow_s = 0, oh_s = 0, sm_s = 0, sr_s = 0;
    dfxisp_accel(sat, sat_out, W, H, DFXISP_MODE_LOW_LIGHT, 512, &ow_s, &oh_s, &sm_s, &sr_s);
    assert(red(sat_out[0]) == 255 && green(sat_out[0]) == 255 && blue(sat_out[0]) == 255);

    // Chroma preservation (adversarial-review regression, 2026-07-02): a 2x2 RGGB
    // cell that is pure-red-in-RAW-space (R sample saturated, both G samples and
    // the B sample at 0) must survive low-light binning-demosaic as a clearly
    // red-dominant pixel, not collapse toward gray. This is the case that a
    // scalar-average-then-redemosaic bug (fixed 2026-07-02) would fail: averaging
    // all 4 samples first destroys the information this assertion checks for.
    {
        uint16_t red_cell[4] = {
            4095, 0,     // R=top-left(4095), G=top-right(0)
            0,    0,     // G=bottom-left(0), B=bottom-right(0)
        };
        uint32_t red_out[1] = {};
        int ow_r = 0, oh_r = 0, sm_r = 0, sr_r = 0;
        dfxisp_accel(red_cell, red_out, 2, 2, DFXISP_MODE_LOW_LIGHT, 512,
                    &ow_r, &oh_r, &sm_r, &sr_r);
        assert(ow_r == 1 && oh_r == 1);
        // must be clearly red-dominant: this would fail under the old scalar-bin
        // bug, where all 4 samples get averaged into one value before demosaic,
        // producing a near-gray pixel instead.
        assert(red(red_out[0]) > green(red_out[0]) + 50);
        assert(red(red_out[0]) > blue(red_out[0]) + 50);
    }

    check_golden_vectors("tests/golden_vectors.csv");

    std::cout << "DFXISP C-sim smoke tests passed\n";
    return 0;
}
