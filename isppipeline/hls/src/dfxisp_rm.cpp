#include "dfxisp_rm.hpp"

#include <cstdint>
#include <vector>

namespace {

// Fixed parameters (must match tools/rm_model.py).
constexpr int GAIN_NUM = 3, GAIN_DEN = 2, LIFT = 8;
constexpr int KNEE = 128, KNEE_LIFT = 40;
// FP redesign: base/detail decomposition with green-guided detail add-back.
constexpr int FP_NOISE_T = 2;
constexpr int FP_DG_EDGE_NUM = 3, FP_DG_EDGE_DEN = 2;
constexpr int FP_DG_FLAT_NUM = 1, FP_DG_FLAT_DEN = 1;

// Floor division identical to Python '//' for any signs (b > 0 here).
static inline int idiv_floor(int a, int b) {
    int q = a / b;
    int r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) --q;
    return q;
}

static inline int clamp_u8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
static inline int raw12_to_u8(int v) { return ((v > 4095 ? 4095 : v) >> 4) & 0xFF; }
static inline uint32_t pack_rgb(int r, int g, int b) {
    return (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static inline int samp(const uint16_t* raw, int w, int h, int x, int y) {
    return raw[clampi(y, 0, h - 1) * w + clampi(x, 0, w - 1)];
}

static void demosaic_pixel(const uint16_t* raw, int w, int h, int x, int y,
                           int& r, int& g, int& b) {
    int win[3][3];
    for (int wy = 0; wy < 3; ++wy)
        for (int wx = 0; wx < 3; ++wx)
            win[wy][wx] = samp(raw, w, h, x + wx - 1, y + wy - 1);
    bool ey = (y & 1) == 0, ex = (x & 1) == 0;
    int c = win[1][1], rr = 0, gg = 0, bb = 0;
    if (ey && ex) { gg = c; rr = (win[1][0] + win[1][2]) / 2; bb = (win[0][1] + win[2][1]) / 2; }
    else if (ey && !ex) { rr = c; gg = (win[1][0] + win[1][2] + win[0][1] + win[2][1]) / 4;
                          bb = (win[0][0] + win[0][2] + win[2][0] + win[2][2]) / 4; }
    else if (!ey && ex) { bb = c; gg = (win[1][0] + win[1][2] + win[0][1] + win[2][1]) / 4;
                          rr = (win[0][0] + win[0][2] + win[2][0] + win[2][2]) / 4; }
    else { gg = c; rr = (win[0][1] + win[2][1]) / 2; bb = (win[1][0] + win[1][2]) / 2; }
    r = raw12_to_u8(rr); g = raw12_to_u8(gg); b = raw12_to_u8(bb);
}

static inline int reg_gain(int v) { return clamp_u8((v * GAIN_NUM) / GAIN_DEN + LIFT); }

static inline int soft_knee(int v) {
    if (v < KNEE) return clamp_u8(v + (KNEE_LIFT * (KNEE - v)) / KNEE);
    return v;
}

static int cmean3x3(const std::vector<int>& ch, int w, int h, int x, int y) {
    int s = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            s += ch[clampi(y + dy, 0, h - 1) * w + clampi(x + dx, 0, w - 1)];
    return s / 9;
}

}  // namespace

extern "C" void dfxisp_accel_variant(
    const uint16_t* raw_bayer, uint32_t* rgb_out, int width, int height, int variant) {
    if (!raw_bayer || !rgb_out || width <= 0 || height <= 0) return;
    const int n = width * height;
    std::vector<int> R(n), G(n), B(n);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            demosaic_pixel(raw_bayer, width, height, x, y, R[y * width + x], G[y * width + x], B[y * width + x]);

    if (variant == DFXISP_RM_STATIC) {
        for (int i = 0; i < n; ++i) rgb_out[i] = pack_rgb(R[i], G[i], B[i]);
        return;
    }
    if (variant == DFXISP_RM_REG_ONLY) {
        for (int i = 0; i < n; ++i) rgb_out[i] = pack_rgb(reg_gain(R[i]), reg_gain(G[i]), reg_gain(B[i]));
        return;
    }
    if (variant == DFXISP_RM_DFX_BIN) {
        // 2x2 bin + gain, then integer 2x bilinear upsample (9/3/3/1 over 16).
        const int hw = height / 2 > 0 ? height / 2 : 1;
        const int ww = width / 2 > 0 ? width / 2 : 1;
        std::vector<int> Rb(hw * ww), Gb(hw * ww), Bb(hw * ww);
        for (int by = 0; by < hw; ++by)
            for (int bx = 0; bx < ww; ++bx) {
                int y0 = 2 * by, y1 = clampi(2 * by + 1, 0, height - 1);
                int x0 = 2 * bx, x1 = clampi(2 * bx + 1, 0, width - 1);
                int sr = R[y0 * width + x0] + R[y0 * width + x1] + R[y1 * width + x0] + R[y1 * width + x1];
                int sg = G[y0 * width + x0] + G[y0 * width + x1] + G[y1 * width + x0] + G[y1 * width + x1];
                int sb = B[y0 * width + x0] + B[y0 * width + x1] + B[y1 * width + x0] + B[y1 * width + x1];
                Rb[by * ww + bx] = reg_gain(sr / 4);
                Gb[by * ww + bx] = reg_gain(sg / 4);
                Bb[by * ww + bx] = reg_gain(sb / 4);
            }
        for (int y = 0; y < height; ++y) {
            int by = clampi(y / 2, 0, hw - 1);
            int nby = ((y & 1) == 0) ? by - 1 : by + 1; nby = clampi(nby, 0, hw - 1);
            for (int x = 0; x < width; ++x) {
                int bx = clampi(x / 2, 0, ww - 1);
                int nbx = ((x & 1) == 0) ? bx - 1 : bx + 1; nbx = clampi(nbx, 0, ww - 1);
                int rr = (9 * Rb[by * ww + bx] + 3 * Rb[by * ww + nbx] + 3 * Rb[nby * ww + bx] + Rb[nby * ww + nbx]) / 16;
                int gg = (9 * Gb[by * ww + bx] + 3 * Gb[by * ww + nbx] + 3 * Gb[nby * ww + bx] + Gb[nby * ww + nbx]) / 16;
                int bv = (9 * Bb[by * ww + bx] + 3 * Bb[by * ww + nbx] + 3 * Bb[nby * ww + bx] + Bb[nby * ww + nbx]) / 16;
                rgb_out[y * width + x] = pack_rgb(rr, gg, bv);
            }
        }
        return;
    }
    if (variant == DFXISP_RM_DFX_FP) {
        // base/detail decomposition: lift base brightness, add detail back with
        // green-guided gain (>=1) -> features preserved/sharpened while darks lift.
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                int i = y * width + x;
                int rb = cmean3x3(R, width, height, x, y);
                int gb = cmean3x3(G, width, height, x, y);
                int bb = cmean3x3(B, width, height, x, y);
                int rd = R[i] - rb, gd = G[i] - gb, bd = B[i] - bb;
                int agd = gd < 0 ? -gd : gd;
                int num = (agd <= FP_NOISE_T) ? FP_DG_FLAT_NUM : FP_DG_EDGE_NUM;
                int den = (agd <= FP_NOISE_T) ? FP_DG_FLAT_DEN : FP_DG_EDGE_DEN;
                int rr = clamp_u8(soft_knee(rb) + idiv_floor(num * rd, den));
                int gg = clamp_u8(soft_knee(gb) + idiv_floor(num * gd, den));
                int bv = clamp_u8(soft_knee(bb) + idiv_floor(num * bd, den));
                rgb_out[i] = pack_rgb(rr, gg, bv);
            }
        return;
    }
}
