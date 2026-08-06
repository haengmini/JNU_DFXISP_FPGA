#include "default_isp.hpp"
#include "golden_csv.hpp"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>

static uint8_t red(uint32_t p) { return uint8_t((p >> 16) & 0xff); }
static uint8_t green(uint32_t p) { return uint8_t((p >> 8) & 0xff); }
static uint8_t blue(uint32_t p) { return uint8_t(p & 0xff); }

static void check_golden_vectors(const char* path) {
    const std::vector<GoldenCase> cases = load_golden_csv(path);
    if (cases.empty()) {
        std::cout << "default_ISP golden vector compare skipped (" << path << " not found)\n";
        return;
    }

    int checked = 0;
    for (const GoldenCase& c : cases) {
        const int in_w = c.param("in_w"), in_h = c.param("in_h");
        const int exp_w = c.param("out_w"), exp_h = c.param("out_h");
        std::vector<uint32_t> got(in_w * in_h, 0);
        int out_w = 0, out_h = 0;
        default_isp(c.raw.data(), got.data(), in_w, in_h, c.param("awb_mode"), &out_w, &out_h);
        // shape-preserving pipeline (no Policy A halving in this arm)
        assert(out_w == exp_w && out_h == exp_h);
        assert(out_w == in_w && out_h == in_h);
        for (int i = 0; i < exp_w * exp_h; ++i) {
            if (got[i] != c.expected[i]) {
                std::cerr << "default_ISP golden mismatch case=" << c.name << " index=" << i
                          << " expected=0x" << std::hex << c.expected[i]
                          << " got=0x" << got[i] << std::dec << "\n";
                assert(got[i] == c.expected[i]);
            }
            ++checked;
        }
    }
    std::cout << "default_ISP golden vector compare passed (" << checked << " pixels)\n";
}

int main(int argc, char** argv) {
    check_golden_vectors(argc > 1 ? argv[1] : "tests/default_isp_golden_vectors.csv");

    constexpr int W = 8, H = 8;

    // Stage (1) blackLevelCorrection sits in the Bayer domain, BEFORE demosaic:
    // a frame entirely at/below the pedestal (32 in this 12-bit domain) must
    // produce pure black -- no later stage can lift it (AWB gains are clamped
    // and gray-world of an all-zero frame is neutral).
    {
        uint16_t at_pedestal[W * H];
        for (int i = 0; i < W * H; ++i) at_pedestal[i] = 32;
        uint32_t out[W * H] = {};
        int ow = 0, oh = 0;
        default_isp(at_pedestal, out, W, H, DEFAULT_ISP_AWB_ON, &ow, &oh);
        for (int i = 0; i < W * H; ++i) assert(out[i] == 0u);
    }

    // Stage (4) AWB is live and adaptive: on a strong blue cast, enabling AWB
    // must pull the channel means closer together than the bypass path.
    {
        uint16_t cast[W * H];
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const bool even_y = (y & 1) == 0, even_x = (x & 1) == 0;
                uint16_t v;
                if (even_y && even_x) v = 400;              // R sites
                else if (!even_y && !even_x) v = 3200;      // B sites
                else v = 1800;                              // G sites
                cast[y * W + x] = v;
            }
        }
        auto imbalance = [&](int awb_mode) {
            uint32_t out[W * H] = {};
            int ow = 0, oh = 0;
            default_isp(cast, out, W, H, awb_mode, &ow, &oh);
            long sr = 0, sg = 0, sb = 0;
            for (int i = 0; i < W * H; ++i) {
                sr += red(out[i]);
                sg += green(out[i]);
                sb += blue(out[i]);
            }
            const long hi = sr > sg ? (sr > sb ? sr : sb) : (sg > sb ? sg : sb);
            const long lo = sr < sg ? (sr < sb ? sr : sb) : (sg < sb ? sg : sb);
            return hi - lo;
        };
        const long off = imbalance(DEFAULT_ISP_AWB_OFF);
        const long on = imbalance(DEFAULT_ISP_AWB_ON);
        assert(on < off);  // adaptive AWB reduces the cast
    }

    // Stage (5) CCM rows sum to 256, so a neutral input stays neutral after
    // AWB: the three channel means must land within a small band of each other.
    {
        uint16_t flat[W * H];
        for (int i = 0; i < W * H; ++i) flat[i] = 1600;
        uint32_t out[W * H] = {};
        int ow = 0, oh = 0;
        default_isp(flat, out, W, H, DEFAULT_ISP_AWB_ON, &ow, &oh);
        long sr = 0, sg = 0, sb = 0;
        for (int i = 0; i < W * H; ++i) {
            sr += red(out[i]);
            sg += green(out[i]);
            sb += blue(out[i]);
        }
        const long n = W * H;
        const long mr = sr / n, mg = sg / n, mb = sb / n;
        assert(std::abs(mr - mg) <= 8 && std::abs(mb - mg) <= 8);
    }

    // Stages (6)+(7): a fully saturated frame never overflows RGB8.
    {
        uint16_t sat[W * H];
        for (int i = 0; i < W * H; ++i) sat[i] = 4095;
        uint32_t out[W * H] = {};
        int ow = 0, oh = 0;
        default_isp(sat, out, W, H, DEFAULT_ISP_AWB_ON, &ow, &oh);
        for (int i = 0; i < W * H; ++i) {
            assert(red(out[i]) <= 255 && green(out[i]) <= 255 && blue(out[i]) <= 255);
        }
    }

    // DFX contract: rm_default_isp_top must be a drop-in for the RP slot --
    // same 6-argument signature as rm_normal_tone_top / rm_low_light_tone_top,
    // shape preserving, and behaviourally identical to default_isp(AWB_ON).
    {
        uint16_t frame[W * H];
        for (int i = 0; i < W * H; ++i) frame[i] = static_cast<uint16_t>((i * 61) % 4096);
        uint32_t via_top[W * H] = {}, via_dev[W * H] = {};
        int ow1 = 0, oh1 = 0, ow2 = 0, oh2 = 0;
        rm_default_isp_top(frame, via_top, W, H, &ow1, &oh1);
        default_isp(frame, via_dev, W, H, DEFAULT_ISP_AWB_ON, &ow2, &oh2);
        assert(ow1 == W && oh1 == H && ow1 == ow2 && oh1 == oh2);
        for (int i = 0; i < W * H; ++i) assert(via_top[i] == via_dev[i]);
    }

    // Degenerate inputs must not crash or write metadata garbage.
    {
        uint32_t out[4] = {};
        int ow = -1, oh = -1;
        default_isp(nullptr, out, W, H, DEFAULT_ISP_AWB_ON, &ow, &oh);
        assert(ow == 0 && oh == 0);
        uint16_t one = 500;
        uint32_t o1 = 0;
        default_isp(&one, &o1, 1, 1, DEFAULT_ISP_AWB_ON, &ow, &oh);
        assert(ow == 1 && oh == 1);
    }

    std::cout << "default_ISP C-sim smoke tests passed\n";
    return 0;
}
