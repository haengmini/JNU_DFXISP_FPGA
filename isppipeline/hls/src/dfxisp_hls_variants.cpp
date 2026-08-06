// HLS-synthesizable variant tops for DFXISP resource/power evidence (direction A).
// Each top is a separate synthesis target (set DFXISP_HLS_TOP). No std::vector.
// Integer ops mirror tools/rm_model.py / src/dfxisp_rm.cpp.
//
// Tops:
//   dfxisp_normal   raw -> demosaic -> RGB         (static-region normal path)
//   dfxisp_regonly  raw -> demosaic -> gain -> RGB (register-only adaptation)
//   dfxisp_bin_rm   RGB -> 2x2 bin + gain + bilinear -> RGB  (DFX-Bin RM = RP module)
//   dfxisp_fp_rm    RGB -> base/detail add-back -> RGB        (DFX-FP RM, ablation)
//
// Resource model (direction A):
//   static all-resident ~= dfxisp_regonly + dfxisp_bin_rm (both resident)
//   DFX normal-mode footprint ~= dfxisp_regonly ; binning loaded on demand (bin_rm partial)
#include <cstdint>

namespace {
constexpr int GAIN_NUM = 3, GAIN_DEN = 2, LIFT = 8;
constexpr int KNEE = 128, KNEE_LIFT = 40;
constexpr int FP_NOISE_T = 2;
constexpr int FP_DG_EDGE_NUM = 3, FP_DG_EDGE_DEN = 2;

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int clamp_u8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
static inline int raw12_to_u8(int v) { return ((v > 4095 ? 4095 : v) >> 4) & 0xFF; }
static inline uint32_t pack_rgb(int r, int g, int b) {
    return (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}
static inline int reg_gain(int v) { return clamp_u8((v * GAIN_NUM) / GAIN_DEN + LIFT); }
static inline int soft_knee(int v) { return v < KNEE ? clamp_u8(v + (KNEE_LIFT * (KNEE - v)) / KNEE) : v; }
static inline int idiv_floor(int a, int b) {
    int q = a / b, r = a % b; if (r != 0 && ((r < 0) != (b < 0))) --q; return q;
}

static inline int rawc(const uint16_t* raw, int w, int h, int x, int y) {
    return raw[clampi(y, 0, h - 1) * w + clampi(x, 0, w - 1)];
}
static void demosaic(const uint16_t* raw, int w, int h, int x, int y, int& r, int& g, int& b) {
#pragma HLS INLINE
    int p[3][3];
    for (int wy = 0; wy < 3; ++wy)
        for (int wx = 0; wx < 3; ++wx) p[wy][wx] = rawc(raw, w, h, x + wx - 1, y + wy - 1);
    bool ey = (y & 1) == 0, ex = (x & 1) == 0; int c = p[1][1], rr = 0, gg = 0, bb = 0;
    if (ey && ex) { gg = c; rr = (p[1][0] + p[1][2]) / 2; bb = (p[0][1] + p[2][1]) / 2; }
    else if (ey && !ex) { rr = c; gg = (p[1][0] + p[1][2] + p[0][1] + p[2][1]) / 4; bb = (p[0][0] + p[0][2] + p[2][0] + p[2][2]) / 4; }
    else if (!ey && ex) { bb = c; gg = (p[1][0] + p[1][2] + p[0][1] + p[2][1]) / 4; rr = (p[0][0] + p[0][2] + p[2][0] + p[2][2]) / 4; }
    else { gg = c; rr = (p[0][1] + p[2][1]) / 2; bb = (p[1][0] + p[1][2]) / 2; }
    r = raw12_to_u8(rr); g = raw12_to_u8(gg); b = raw12_to_u8(bb);
}
static inline void get_rgb(const uint32_t* in, int w, int h, int x, int y, int& r, int& g, int& b) {
    uint32_t v = in[clampi(y, 0, h - 1) * w + clampi(x, 0, w - 1)];
    r = (v >> 16) & 0xFF; g = (v >> 8) & 0xFF; b = v & 0xFF;
}
// reg-gained 2x2 binned cell (by,bx in binned grid coords)
static void bin_cell(const uint32_t* in, int w, int h, int by, int bx, int& r, int& g, int& b) {
#pragma HLS INLINE
    int sr = 0, sg = 0, sb = 0;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            int rr, gg, bb; get_rgb(in, w, h, 2 * bx + i, 2 * by + j, rr, gg, bb);
            sr += rr; sg += gg; sb += bb;
        }
    r = reg_gain(sr / 4); g = reg_gain(sg / 4); b = reg_gain(sb / 4);
}
}  // namespace

extern "C" void dfxisp_normal(const uint16_t* raw_bayer, uint32_t* rgb_out, int width, int height) {
#pragma HLS INTERFACE m_axi port=raw_bayer offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=rgb_out offset=slave bundle=gmem1
#pragma HLS INTERFACE s_axilite port=raw_bayer bundle=control
#pragma HLS INTERFACE s_axilite port=rgb_out bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    if (!raw_bayer || !rgb_out || width <= 0 || height <= 0) return;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=64 max=2073600
            int r, g, b; demosaic(raw_bayer, width, height, x, y, r, g, b);
            rgb_out[y * width + x] = pack_rgb(r, g, b);
        }
}

extern "C" void dfxisp_regonly(const uint16_t* raw_bayer, uint32_t* rgb_out, int width, int height) {
#pragma HLS INTERFACE m_axi port=raw_bayer offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=rgb_out offset=slave bundle=gmem1
#pragma HLS INTERFACE s_axilite port=raw_bayer bundle=control
#pragma HLS INTERFACE s_axilite port=rgb_out bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    if (!raw_bayer || !rgb_out || width <= 0 || height <= 0) return;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=64 max=2073600
            int r, g, b; demosaic(raw_bayer, width, height, x, y, r, g, b);
            rgb_out[y * width + x] = pack_rgb(reg_gain(r), reg_gain(g), reg_gain(b));
        }
}

extern "C" void dfxisp_bin_rm(const uint32_t* rgb_in, uint32_t* rgb_out, int width, int height) {
#pragma HLS INTERFACE m_axi port=rgb_in offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=rgb_out offset=slave bundle=gmem1
#pragma HLS INTERFACE s_axilite port=rgb_in bundle=control
#pragma HLS INTERFACE s_axilite port=rgb_out bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    if (!rgb_in || !rgb_out || width <= 0 || height <= 0) return;
    int hw = height / 2 > 0 ? height / 2 : 1, ww = width / 2 > 0 ? width / 2 : 1;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
#pragma HLS PIPELINE
#pragma HLS LOOP_TRIPCOUNT min=64 max=2073600
            int by = clampi(y / 2, 0, hw - 1), bx = clampi(x / 2, 0, ww - 1);
            int nby = clampi((y & 1) == 0 ? by - 1 : by + 1, 0, hw - 1);
            int nbx = clampi((x & 1) == 0 ? bx - 1 : bx + 1, 0, ww - 1);
            int r0, g0, b0, r1, g1, b1, r2, g2, b2, r3, g3, b3;
            bin_cell(rgb_in, width, height, by, bx, r0, g0, b0);
            bin_cell(rgb_in, width, height, by, nbx, r1, g1, b1);
            bin_cell(rgb_in, width, height, nby, bx, r2, g2, b2);
            bin_cell(rgb_in, width, height, nby, nbx, r3, g3, b3);
            int r = (9 * r0 + 3 * r1 + 3 * r2 + r3) / 16;
            int g = (9 * g0 + 3 * g1 + 3 * g2 + g3) / 16;
            int b = (9 * b0 + 3 * b1 + 3 * b2 + b3) / 16;
            rgb_out[y * width + x] = pack_rgb(r, g, b);
        }
}

extern "C" void dfxisp_fp_rm(const uint32_t* rgb_in, uint32_t* rgb_out, int width, int height) {
#pragma HLS INTERFACE m_axi port=rgb_in offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=rgb_out offset=slave bundle=gmem1
#pragma HLS INTERFACE s_axilite port=rgb_in bundle=control
#pragma HLS INTERFACE s_axilite port=rgb_out bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    if (!rgb_in || !rgb_out || width <= 0 || height <= 0) return;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
#pragma HLS PIPELINE
#pragma HLS LOOP_TRIPCOUNT min=64 max=2073600
            int sr = 0, sg = 0, sb = 0, cr = 0, cg = 0, cb = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    int rr, gg, bb; get_rgb(rgb_in, width, height, x + dx, y + dy, rr, gg, bb);
                    sr += rr; sg += gg; sb += bb;
                    if (dx == 0 && dy == 0) { cr = rr; cg = gg; cb = bb; }
                }
            int rb = sr / 9, gb = sg / 9, bb_ = sb / 9;
            int rd = cr - rb, gd = cg - gb, bd = cb - bb_;
            int agd = gd < 0 ? -gd : gd;
            int num = (agd <= FP_NOISE_T) ? 1 : FP_DG_EDGE_NUM;
            int den = (agd <= FP_NOISE_T) ? 1 : FP_DG_EDGE_DEN;
            int r = clamp_u8(soft_knee(rb) + idiv_floor(num * rd, den));
            int g = clamp_u8(soft_knee(gb) + idiv_floor(num * gd, den));
            int b = clamp_u8(soft_knee(bb_) + idiv_floor(num * bd, den));
            rgb_out[y * width + x] = pack_rgb(r, g, b);
        }
}
