#include "dfxisp_accel.hpp"

#include <cstdint>

// =============================================================================
// DFXISP core C-sim — shared baseline ISP core + mutually exclusive tone RM
// slot. Integer-only; bit-exact mirror of the Python golden model.
// Updated: 2026-07-20 (BLC 2/2 + checker C1). Design history, bug-fix and
// constant rationale: see dfxisp_accel.md (same directory).
// =============================================================================

namespace {

// --- shared baseline-core parameters (12-bit RAW domain) ---
// BLC recalibration (2026-07-20, approved): real-sensor RAW sweeps put the mAP
// peak at black level 1~2 for every arm/split; both modes share 2. Evidence:
// dfxisp_accel.md §2.
constexpr int BLC_OFFSET12 = 2 << 4;   // black level 2 (8-bit) -> 32 (12-bit), normal mode
constexpr int BLC_OFFSET12_LOWLIGHT = 2 << 4;   // black level 2 (8-bit) -> 32 (12-bit)
constexpr int RAW12_MAX = 4095;
constexpr int AWB_R = 286;              // Q8 per-channel white balance (color)
constexpr int AWB_G = 256;
constexpr int AWB_B = 307;
// --- tone RM parameters (exposure gain per mode + gamma) ---
constexpr int GAIN_NORMAL_NUM = 5, GAIN_NORMAL_DEN = 4;      // normal 1.25x
constexpr int GAIN_LOWLIGHT_NUM = 2, GAIN_LOWLIGHT_DEN = 1;  // low-light 2.0x
// gamma 2.0 realized exactly as integer sqrt: 255*(v/255)^(1/2) = floor(sqrt(255*v))
// --- checker ---
// Checker C1 (deployed 2026-07-20): AUTO -> LOW_LIGHT when dark16 ratio > 0.62.
// Driver must write dark_pixel_threshold = 256 (= 16<<4 in this raw12 domain).
// Selection evidence and metrics: dfxisp_accel.md §2.
constexpr int DARK_RATIO_PCT = 62;      // AUTO -> LOW_LIGHT when dark pixels > 62%
// Schmitt band (C1 spec, checker-principles-2026-07-05 principle 5): delta =
// 2%p around the 62% center -> ENTER LOW_LIGHT above 64%, EXIT below 60%.
// The asymmetry vs the single-frame verdict (62) is intentional: enter at 64
// lowers false triggers, exit at 60 keeps recall on already-dark scenes.
// The mode state itself lives in the static-region RTL block
// results/pr_controller/checker_hysteresis.v; this core only exports the two
// band-compare flags per frame (dfxisp_accel.md §7).
constexpr int HYST_ENTER_PCT = 64;
constexpr int HYST_EXIT_PCT = 60;

static inline int clamp_i(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline uint8_t clamp_u8(int v) { return static_cast<uint8_t>(clamp_i(v, 0, 255)); }

static inline uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

// gamma 2.0 tone: out = floor(sqrt(255 * v)), v,out in [0,255]. 256-entry ROM
// (small BRAM/LUT), values generated once via the same formula as the Python
// golden model (tools/gen_golden_vectors.py gamma2(); isqrt(255*v)) — plain
// static array so it synthesizes as a ROM without a runtime iterative sqrt.
static const uint8_t GAMMA2_LUT[256] = {
      0, 15, 22, 27, 31, 35, 39, 42, 45, 47, 50, 52, 55, 57, 59, 61,
     63, 65, 67, 69, 71, 73, 74, 76, 78, 79, 81, 82, 84, 85, 87, 88,
     90, 91, 93, 94, 95, 97, 98, 99,100,102,103,104,105,107,108,109,
    110,111,112,114,115,116,117,118,119,120,121,122,123,124,125,126,
    127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,141,
    142,143,144,145,146,147,148,148,149,150,151,152,153,153,154,155,
    156,157,158,158,159,160,161,162,162,163,164,165,165,166,167,168,
    168,169,170,171,171,172,173,174,174,175,176,177,177,178,179,179,
    180,181,182,182,183,184,184,185,186,186,187,188,188,189,190,190,
    191,192,192,193,194,194,195,196,196,197,198,198,199,200,200,201,
    201,202,203,203,204,205,205,206,206,207,208,208,209,210,210,211,
    211,212,213,213,214,214,215,216,216,217,217,218,218,219,220,220,
    221,221,222,222,223,224,224,225,225,226,226,227,228,228,229,229,
    230,230,231,231,232,233,233,234,234,235,235,236,236,237,237,238,
    238,239,240,240,241,241,242,242,243,243,244,244,245,245,246,246,
    247,247,248,248,249,249,250,250,251,251,252,252,253,253,254,255,
};

static inline uint8_t gamma2(uint8_t v) {
#pragma HLS INLINE
    return GAMMA2_LUT[v];
}

static inline uint16_t sample_clamped(const uint16_t* raw, int width, int height, int x, int y) {
    x = x < 0 ? 0 : (x >= width ? width - 1 : x);
    y = y < 0 ? 0 : (y >= height ? height - 1 : y);
    return raw[y * width + x];
}

// Max binned-row width for the low-light streaming line buffer (RESEARCH frame
// budget: dev 640x480 .. eval 1280x720; matches existing LOOP_TRIPCOUNT max=960
// on the binned-grid loops, i.e. supports raw width up to 1920).
constexpr int MAX_BINNED_W = 960;

// ---------------------------------------------------------------------------
// Scene checker / mode decision (static region). Dark-pixel ratio on RAW.
// ---------------------------------------------------------------------------
static int checker_select_mode(const uint16_t* raw, int width, int height, int mode,
                               uint16_t dark_pixel_threshold, int& hyst_flags) {
    // Forced modes report flags matching the forced state so the hysteresis
    // block (if wired) tracks the override instead of fighting it.
    if (mode == DFXISP_MODE_NORMAL) {
        hyst_flags = DFXISP_HYST_BELOW_EXIT;
        return DFXISP_MODE_NORMAL;
    }
    if (mode == DFXISP_MODE_LOW_LIGHT) {
        hyst_flags = DFXISP_HYST_ABOVE_ENTER;
        return DFXISP_MODE_LOW_LIGHT;
    }
    const int n = width * height;
    int dark = 0;
    for (int i = 0; i < n; ++i) {
#pragma HLS LOOP_TRIPCOUNT min=16 max=2073600
        if (raw[i] < dark_pixel_threshold) ++dark;
    }
    const bool above_enter = dark * 100 > HYST_ENTER_PCT * n;
    const bool below_exit = dark * 100 < HYST_EXIT_PCT * n;
    hyst_flags = (above_enter ? DFXISP_HYST_ABOVE_ENTER : 0) |
                 (below_exit ? DFXISP_HYST_BELOW_EXIT : 0);
    // The single-frame verdict (Arm2 runtime branch / golden contract) keeps
    // the deployed C1 threshold (62), independent of the Schmitt band edges;
    // the Schmitt state machine consumes the flags outside this core.
    return (dark * 100 > DARK_RATIO_PCT * n) ? DFXISP_MODE_LOW_LIGHT : DFXISP_MODE_NORMAL;
}

// ---------------------------------------------------------------------------
// RGGB demosaic keeping 12-bit precision (no >>4 here). Pattern:
//   (0,0)=R (0,1)=G (1,0)=G (1,1)=B
// ---------------------------------------------------------------------------
static void demosaic_rggb12(const uint16_t* raw, int width, int height, int x, int y,
                            int& r12, int& g12, int& b12) {
#pragma HLS INLINE
    uint16_t win[3][3];
    for (int wy = 0; wy < 3; ++wy)
        for (int wx = 0; wx < 3; ++wx)
            win[wy][wx] = sample_clamped(raw, width, height, x + wx - 1, y + wy - 1);

    const bool even_y = (y & 1) == 0;
    const bool even_x = (x & 1) == 0;
    const uint16_t c = win[1][1];
    int rr = 0, gg = 0, bb = 0;

    if (even_y && even_x) {          // R
        rr = c;
        gg = (win[1][0] + win[1][2] + win[0][1] + win[2][1]) / 4;
        bb = (win[0][0] + win[0][2] + win[2][0] + win[2][2]) / 4;
    } else if (even_y && !even_x) {  // G on R row (R horizontal, B vertical)
        gg = c;
        rr = (win[1][0] + win[1][2]) / 2;
        bb = (win[0][1] + win[2][1]) / 2;
    } else if (!even_y && even_x) {  // G on B row (R vertical, B horizontal)
        gg = c;
        rr = (win[0][1] + win[2][1]) / 2;
        bb = (win[1][0] + win[1][2]) / 2;
    } else {                         // B
        bb = c;
        gg = (win[1][0] + win[1][2] + win[0][1] + win[2][1]) / 4;
        rr = (win[0][0] + win[0][2] + win[2][0] + win[2][2]) / 4;
    }
    r12 = rr > RAW12_MAX ? RAW12_MAX : rr;
    g12 = gg > RAW12_MAX ? RAW12_MAX : gg;
    b12 = bb > RAW12_MAX ? RAW12_MAX : bb;
}

// ---------------------------------------------------------------------------
// Shared baseline ISP core (ver1): BLC + WB + CCM, all in 12-bit. No gain/
// gamma. Returns 12-bit R,G,B (tone stage does >>4 + gamma). Consumes
// already-demosaiced R,G,B triples -- callers supply them either from a full
// Bayer demosaic (normal path) or from the low-light binning-demosaic (which
// IS the demosaic step for the binned grid; no second pass here). WB/CCM are
// identical between modes (still one code path); `blc_offset` is the one
// mode-specific parameter (see BLC_OFFSET12_LOWLIGHT comment above).
// ---------------------------------------------------------------------------
static inline void apply_blc_wb12(int dr, int dg, int db, int blc_offset,
                                  int& r12, int& g12, int& b12) {
#pragma HLS INLINE
    dr = clamp_i(dr - blc_offset, 0, RAW12_MAX);                   // BLC (subtract first)
    dg = clamp_i(dg - blc_offset, 0, RAW12_MAX);
    db = clamp_i(db - blc_offset, 0, RAW12_MAX);
    r12 = clamp_i(dr * AWB_R / 256, 0, RAW12_MAX);                 // WB per channel (Q8)
    g12 = clamp_i(dg * AWB_G / 256, 0, RAW12_MAX);
    b12 = clamp_i(db * AWB_B / 256, 0, RAW12_MAX);                 // CCM identity (no gain/gamma)
}

static void baseline_core12(const uint16_t* raw, int width, int height, int x, int y,
                            int& r12, int& g12, int& b12) {
#pragma HLS INLINE
    int dr, dg, db;
    demosaic_rggb12(raw, width, height, x, y, dr, dg, db);         // demosaic (12-bit)
    apply_blc_wb12(dr, dg, db, BLC_OFFSET12, r12, g12, b12);
}

// tone RM: exposure gain (12-bit) -> >>4 to 8-bit -> gamma 2.0. Mode-specific gain.
static inline uint8_t tone(int v12, int gnum, int gden) {
#pragma HLS INLINE
    const int gained = clamp_i(v12 * gnum / gden, 0, RAW12_MAX);
    return gamma2(clamp_u8(gained >> 4));
}

// ---------------------------------------------------------------------------
// RM_NORMAL_TONE (NORMAL): gain 1.25x + gamma over the full-res baseline core.
// ---------------------------------------------------------------------------
static void run_normal(const uint16_t* raw, uint32_t* rgb_out, int width, int height) {
    for (int y = 0; y < height; ++y) {
#pragma HLS LOOP_TRIPCOUNT min=4 max=1080
        for (int x = 0; x < width; ++x) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=4 max=1920
            int r12, g12, b12;
            baseline_core12(raw, width, height, x, y, r12, g12, b12);     // no gain/gamma
            const uint8_t r = tone(r12, GAIN_NORMAL_NUM, GAIN_NORMAL_DEN);
            const uint8_t g = tone(g12, GAIN_NORMAL_NUM, GAIN_NORMAL_DEN);
            const uint8_t b = tone(b12, GAIN_NORMAL_NUM, GAIN_NORMAL_DEN);
            rgb_out[y * width + x] = pack_rgb(r, g, b);
        }
    }
}

// ---------------------------------------------------------------------------
// RM_LOW_LIGHT_TONE (Policy A, H/2 x W/2):
//   front: 2x2 RAW binning -> baseline core -> back: gain 2.0x + gamma
// ---------------------------------------------------------------------------
static inline int bin_dim(int d) { return d / 2 < 1 ? 1 : d / 2; }

// front: 2x2 RAW binning-demosaic, fused (RESEARCH §4.2 — before precision
// loss). This IS the demosaic step for the binned grid: each RGGB cell's four
// samples are mapped directly to R/G/B (R=top-left, G=avg(top-right,
// bottom-left), B=bottom-right) -- bit-exact match to
// tools/isp_pipeline_ver1.py's `_bin_demosaic_rggb16`. No Bayer-phase
// re-interpolation is applied afterward (that would require the binned grid
// to still look like Bayer data, which it does not once averaged/extracted).
// `by` must already be caller-clamped to [0,bh).
static void compute_binned_rgb_row(const uint16_t* raw, int width, int height, int bw, int by,
                                   uint16_t* row_r, uint16_t* row_g, uint16_t* row_b) {
#pragma HLS INLINE
    const int y0 = 2 * by, y1 = (2 * by + 1 < height) ? 2 * by + 1 : height - 1;
    for (int bx = 0; bx < bw; ++bx) {
#pragma HLS LOOP_TRIPCOUNT min=2 max=960
        const int x0 = 2 * bx, x1 = (2 * bx + 1 < width) ? 2 * bx + 1 : width - 1;
        const uint16_t p00 = raw[y0 * width + x0];  // R (top-left)
        const uint16_t p01 = raw[y0 * width + x1];  // G (top-right)
        const uint16_t p10 = raw[y1 * width + x0];  // G (bottom-left)
        const uint16_t p11 = raw[y1 * width + x1];  // B (bottom-right)
        row_r[bx] = p00;
        row_g[bx] = static_cast<uint16_t>((p01 + p10) / 2);
        row_b[bx] = p11;
    }
}

// RM_LOW_LIGHT_TONE (Policy A, H/2 x W/2), streaming line-buffer version:
//   front: 2x2 RAW binning-demosaic -> baseline core -> back: gain 2.0x + gamma
// Resident state is one row each of R/G/B (O(3*MAX_BINNED_W) BRAM, not a
// full-frame scratch copy). No neighbouring-row window is needed here (unlike
// the earlier scalar-then-redemosaic version): binning-demosaic only reads
// the 2 raw rows belonging to its own cell.
static void run_low_light(const uint16_t* raw, uint32_t* rgb_out, int width, int height,
                          int& out_width, int& out_height) {
    const int bw = bin_dim(width);
    const int bh = bin_dim(height);
    out_width = bw;
    out_height = bh;

    static uint16_t row_r[MAX_BINNED_W], row_g[MAX_BINNED_W], row_b[MAX_BINNED_W];

    for (int y = 0; y < bh; ++y) {
#pragma HLS LOOP_TRIPCOUNT min=2 max=540
        compute_binned_rgb_row(raw, width, height, bw, y, row_r, row_g, row_b);

        for (int x = 0; x < bw; ++x) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=2 max=960
            int r12, g12, b12;
            apply_blc_wb12(row_r[x], row_g[x], row_b[x],
                           BLC_OFFSET12_LOWLIGHT, r12, g12, b12);    // relaxed BLC (2026-07-03)
            const uint8_t r = tone(r12, GAIN_LOWLIGHT_NUM, GAIN_LOWLIGHT_DEN);  // gain 2.0x + gamma
            const uint8_t g = tone(g12, GAIN_LOWLIGHT_NUM, GAIN_LOWLIGHT_DEN);
            const uint8_t b = tone(b12, GAIN_LOWLIGHT_NUM, GAIN_LOWLIGHT_DEN);
            rgb_out[y * bw + x] = pack_rgb(r, g, b);
        }
    }
}

}  // namespace

// -----------------------------------------------------------------------------
// Standalone per-RM HLS tops: separately synthesizable so each DFX RM candidate
// gets its own resource/timing numbers. Port lists of the two tops must stay
// IDENTICAL (same RP slot contract). Background: dfxisp_accel.md §5.
// -----------------------------------------------------------------------------
extern "C" void rm_normal_tone_top(
    const uint16_t* raw_bayer, uint32_t* rgb_out, int width, int height,
    int* out_width, int* out_height) {
#pragma HLS INTERFACE m_axi port=raw_bayer offset=slave bundle=gmem0 depth=2048
#pragma HLS INTERFACE m_axi port=rgb_out offset=slave bundle=gmem1 depth=2048
#pragma HLS INTERFACE s_axilite port=raw_bayer bundle=control
#pragma HLS INTERFACE s_axilite port=rgb_out bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=out_width bundle=control
#pragma HLS INTERFACE s_axilite port=out_height bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    if (!raw_bayer || !rgb_out || width <= 0 || height <= 0) {
        if (out_width) *out_width = 0;
        if (out_height) *out_height = 0;
        return;
    }
    run_normal(raw_bayer, rgb_out, width, height);
    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
}

extern "C" void rm_low_light_tone_top(
    const uint16_t* raw_bayer, uint32_t* rgb_out, int width, int height,
    int* out_width, int* out_height) {
#pragma HLS INTERFACE m_axi port=raw_bayer offset=slave bundle=gmem0 depth=2048
#pragma HLS INTERFACE m_axi port=rgb_out offset=slave bundle=gmem1 depth=2048
#pragma HLS INTERFACE s_axilite port=raw_bayer bundle=control
#pragma HLS INTERFACE s_axilite port=rgb_out bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=out_width bundle=control
#pragma HLS INTERFACE s_axilite port=out_height bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    if (!raw_bayer || !rgb_out || width <= 0 || height <= 0) {
        if (out_width) *out_width = 0;
        if (out_height) *out_height = 0;
        return;
    }
    int ow = 0, oh = 0;
    run_low_light(raw_bayer, rgb_out, width, height, ow, oh);
    if (out_width) *out_width = ow;
    if (out_height) *out_height = oh;
}

extern "C" void dfxisp_accel(
    const uint16_t* raw_bayer,
    uint32_t* rgb_out,
    int width,
    int height,
    int mode,
    uint16_t dark_pixel_threshold,
    int* out_width,
    int* out_height,
    int* selected_mode,
    int* selected_rm,
    int* hyst_flags) {
// depth= only sizes cosim's m_axi BFM memory model (no effect on synthesized
// RTL); 2048 = headroom over the cumulative cosim address span. Full story:
// dfxisp_accel.md §6.
#pragma HLS INTERFACE m_axi port=raw_bayer offset=slave bundle=gmem0 depth=2048
#pragma HLS INTERFACE m_axi port=rgb_out offset=slave bundle=gmem1 depth=2048
#pragma HLS INTERFACE s_axilite port=raw_bayer bundle=control
#pragma HLS INTERFACE s_axilite port=rgb_out bundle=control
#pragma HLS INTERFACE s_axilite port=width bundle=control
#pragma HLS INTERFACE s_axilite port=height bundle=control
#pragma HLS INTERFACE s_axilite port=mode bundle=control
#pragma HLS INTERFACE s_axilite port=dark_pixel_threshold bundle=control
#pragma HLS INTERFACE s_axilite port=out_width bundle=control
#pragma HLS INTERFACE s_axilite port=out_height bundle=control
#pragma HLS INTERFACE s_axilite port=selected_mode bundle=control
#pragma HLS INTERFACE s_axilite port=selected_rm bundle=control
// hyst_flags is a fabric wire (value + ap_vld pulse), NOT an s_axilite
// register: it feeds checker_hysteresis.v in the static region directly.
// One vld pulse per completed frame; no pulse on the invalid-arg early
// return (the hysteresis block simply holds state).
#pragma HLS INTERFACE ap_vld port=hyst_flags
#pragma HLS INTERFACE s_axilite port=return bundle=control

    if (!raw_bayer || !rgb_out || width <= 0 || height <= 0) {
        if (out_width) *out_width = 0;
        if (out_height) *out_height = 0;
        if (selected_mode) *selected_mode = DFXISP_MODE_NORMAL;
        if (selected_rm) *selected_rm = DFXISP_RM_NORMAL_TONE;
        return;
    }

    int flags = 0;
    const int selected = checker_select_mode(raw_bayer, width, height, mode, dark_pixel_threshold, flags);
    if (hyst_flags) *hyst_flags = flags;

    int out_w = width, out_h = height, sel_rm = DFXISP_RM_NORMAL_TONE;
    if (selected == DFXISP_MODE_LOW_LIGHT) {
        run_low_light(raw_bayer, rgb_out, width, height, out_w, out_h);
        sel_rm = DFXISP_RM_LOW_LIGHT_TONE;
    } else {
        run_normal(raw_bayer, rgb_out, width, height);
        sel_rm = DFXISP_RM_NORMAL_TONE;
    }

    if (out_width) *out_width = out_w;
    if (out_height) *out_height = out_h;
    if (selected_mode) *selected_mode = selected;
    if (selected_rm) *selected_rm = sel_rm;
}
