#!/usr/bin/env python3
"""Canonical (bit-exact) reference model for DFXISP RM variants.

This module is the single source of truth for the four experiment variants and
is mirrored byte-for-byte by src/dfxisp_rm.cpp.  Integer-only arithmetic with
explicit floor division so Python and C++ agree exactly.

Variants:
  0 STATIC   : demosaic only (no low-light block)
  1 REG_ONLY : per-pixel gain + lift via registers (no structural change)
  2 DFX_BIN  : 2x2 binning + gain, replicated back to HxW (resolution loss)
  3 DFX_FP   : green-guided feature-preserving (soft-knee + gated local contrast)

Pixel format mirrors dfxisp_accel: GRBG Bayer 12-bit in uint16, packed RGB888.
"""
from __future__ import annotations

VAR_STATIC, VAR_REG, VAR_BIN, VAR_FP = 0, 1, 2, 3
VARIANT_NAMES = {0: "static", 1: "reg_only", 2: "dfx_bin", 3: "dfx_fp"}

# Fixed register/LUT parameters (deterministic constants).
GAIN_NUM, GAIN_DEN, LIFT = 3, 2, 8          # reg gain: v*3/2 + 8
KNEE, KNEE_LIFT = 128, 40                    # FP soft-knee lift applied to the BASE (low-freq)
# FP redesign: base/detail decomposition. Lift base brightness, add detail back
# (>=1 gain) so high frequencies are structurally preserved; green guides the gain.
FP_NOISE_T = 2                               # green detail magnitude below this = flat (no boost)
FP_DG_EDGE_NUM, FP_DG_EDGE_DEN = 3, 2        # detail gain on structure (boost 1.5x)
FP_DG_FLAT_NUM, FP_DG_FLAT_DEN = 1, 1        # detail gain on flat (keep, no noise amp)


def floordiv(a: int, b: int) -> int:
    """Floor division identical in Python and the C++ helper idiv_floor."""
    q = a // b  # Python // is floor for ints
    return q


def clamp_u8(v: int) -> int:
    return 0 if v < 0 else 255 if v > 255 else v


def raw12_to_u8(v: int) -> int:
    return (min(v, 4095) >> 4) & 0xFF


def pack_rgb(r: int, g: int, b: int) -> int:
    return (r << 16) | (g << 8) | b


def _samp(raw, w, h, x, y):
    x = 0 if x < 0 else w - 1 if x >= w else x
    y = 0 if y < 0 else h - 1 if y >= h else y
    return raw[y * w + x]


def demosaic_pixel(raw, w, h, x, y):
    win = [[_samp(raw, w, h, x + wx - 1, y + wy - 1) for wx in range(3)] for wy in range(3)]
    ey, ex, c = (y & 1) == 0, (x & 1) == 0, win[1][1]
    if ey and ex:
        gg = c; rr = (win[1][0] + win[1][2]) // 2; bb = (win[0][1] + win[2][1]) // 2
    elif ey and not ex:
        rr = c; gg = (win[1][0] + win[1][2] + win[0][1] + win[2][1]) // 4
        bb = (win[0][0] + win[0][2] + win[2][0] + win[2][2]) // 4
    elif (not ey) and ex:
        bb = c; gg = (win[1][0] + win[1][2] + win[0][1] + win[2][1]) // 4
        rr = (win[0][0] + win[0][2] + win[2][0] + win[2][2]) // 4
    else:
        gg = c; rr = (win[0][1] + win[2][1]) // 2; bb = (win[1][0] + win[1][2]) // 2
    return raw12_to_u8(rr), raw12_to_u8(gg), raw12_to_u8(bb)


def demosaic_frame(raw, w, h):
    """Return (R,G,B) channel lists of length w*h (uint8)."""
    R = [0] * (w * h); G = [0] * (w * h); B = [0] * (w * h)
    for y in range(h):
        for x in range(w):
            r, g, b = demosaic_pixel(raw, w, h, x, y)
            i = y * w + x
            R[i], G[i], B[i] = r, g, b
    return R, G, B


def _reg_gain(v: int) -> int:
    return clamp_u8((v * GAIN_NUM) // GAIN_DEN + LIFT)


def _soft_knee(v: int) -> int:
    if v < KNEE:
        return clamp_u8(v + (KNEE_LIFT * (KNEE - v)) // KNEE)
    return v


def _cmean3x3(ch, w, h, x, y):
    s = 0
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            xx = 0 if x + dx < 0 else w - 1 if x + dx >= w else x + dx
            yy = 0 if y + dy < 0 else h - 1 if y + dy >= h else y + dy
            s += ch[yy * w + xx]
    return s // 9


def _bin_bilinear(ch, w, h):
    """2x2 bin (sum/4) + reg gain on the binned grid, then integer 2x bilinear
    upsample to full res. Weights 9/3/3/1 over 16 (separable 3/4-1/4 phases).
    Bit-exact mirror in src/dfxisp_rm.cpp."""
    hw, ww = max(1, h // 2), max(1, w // 2)
    B = [[0] * ww for _ in range(hw)]
    for by in range(hw):
        for bx in range(ww):
            y0, y1 = 2 * by, min(2 * by + 1, h - 1)
            x0, x1 = 2 * bx, min(2 * bx + 1, w - 1)
            s = ch[y0 * w + x0] + ch[y0 * w + x1] + ch[y1 * w + x0] + ch[y1 * w + x1]
            B[by][bx] = _reg_gain(s // 4)
    out = [0] * (w * h)
    for y in range(h):
        by = min(y // 2, hw - 1)
        nby = by - 1 if (y & 1) == 0 else by + 1
        nby = 0 if nby < 0 else (hw - 1 if nby >= hw else nby)
        for x in range(w):
            bx = min(x // 2, ww - 1)
            nbx = bx - 1 if (x & 1) == 0 else bx + 1
            nbx = 0 if nbx < 0 else (ww - 1 if nbx >= ww else nbx)
            out[y * w + x] = (9 * B[by][bx] + 3 * B[by][nbx] + 3 * B[nby][bx] + B[nby][nbx]) // 16
    return out


def variant_frame(raw, w, h, variant):
    R, G, B = demosaic_frame(raw, w, h)
    out = [0] * (w * h)

    if variant == VAR_STATIC:
        for i in range(w * h):
            out[i] = pack_rgb(R[i], G[i], B[i])
        return out

    if variant == VAR_REG:
        for i in range(w * h):
            out[i] = pack_rgb(_reg_gain(R[i]), _reg_gain(G[i]), _reg_gain(B[i]))
        return out

    if variant == VAR_BIN:
        # 2x2 binning (denoise) + gain, then integer 2x bilinear upsample (3/4-1/4
        # phases) back to full res. Bilinear (not nearest) keeps mAP within the
        # register-only guardrail in the low-light regime ([08-e2-map-results]).
        Rb = _bin_bilinear(R, w, h)
        Gb = _bin_bilinear(G, w, h)
        Bb = _bin_bilinear(B, w, h)
        for i in range(w * h):
            out[i] = pack_rgb(Rb[i], Gb[i], Bb[i])
        return out

    if variant == VAR_FP:
        # base/detail decomposition: base=3x3 mean (low-freq), detail=px-base (high-freq).
        # Lift base brightness (soft-knee on base) and add detail back with a
        # green-guided gain (>=1) so features are preserved/sharpened while darks
        # brighten. This structurally retains high frequencies (vs binning).
        for y in range(h):
            for x in range(w):
                i = y * w + x
                rb = _cmean3x3(R, w, h, x, y)
                gb = _cmean3x3(G, w, h, x, y)
                bb_ = _cmean3x3(B, w, h, x, y)
                rd, gd, bd = R[i] - rb, G[i] - gb, B[i] - bb_
                if abs(gd) <= FP_NOISE_T:
                    num, den = FP_DG_FLAT_NUM, FP_DG_FLAT_DEN
                else:
                    num, den = FP_DG_EDGE_NUM, FP_DG_EDGE_DEN
                rr = clamp_u8(_soft_knee(rb) + floordiv(num * rd, den))
                gg = clamp_u8(_soft_knee(gb) + floordiv(num * gd, den))
                bbv = clamp_u8(_soft_knee(bb_) + floordiv(num * bd, den))
                out[i] = pack_rgb(rr, gg, bbv)
        return out

    raise ValueError(f"unknown variant {variant}")
