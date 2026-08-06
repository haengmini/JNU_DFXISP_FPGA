# DFXISP HLS C-sim 스캐폴드

로컬 저장소 경로: `isppipeline/hls/`. 정본 아키텍처 문서: 저장소 루트 `RESEARCH.md`.

## 목표 (reset 2026-07-01)

이 스캐폴드는 **shared baseline ISP core + 상호배타 tone RM slot** 구조의 첫
결정적(deterministic) C-시뮬레이션 대상이다. tone RM slot이 shared baseline core를
**감싸는(wrap)** 형태다:

```text
NORMAL:
  pseudo-RAW Bayer RGGB uint16
    -> checker (mode 결정)
    -> RM_NORMAL_TONE (gain 1.25x + gamma2.0)
    -> baseline ISP core (demosaic + BLC + WB + CCM, 12-bit, gain/gamma 없음)
    -> packed RGB888 uint32  (H x W)

LOW_LIGHT:
  pseudo-RAW Bayer RGGB uint16
    -> checker (mode 결정)
    -> RM_LOW_LIGHT_TONE.front : 2x2 RAW binning (precision loss 전, RESEARCH §4.2)
    -> baseline ISP core (demosaic + BLC + WB + CCM, 12-bit, gain/gamma 없음)
    -> RM_LOW_LIGHT_TONE.back  : low-light gain 2.0x + gamma2.0 tone
    -> packed RGB888 uint32  (H/2 x W/2, Policy A 형상변경)
```

C-sim이 증명하는 불변식(RESEARCH.md §8.2):

- 프레임마다 **정확히 하나의 tone RM**만 선택(mutually exclusive).
- gain/gamma는 **tone RM에만** 존재하고 baseline core에는 중복되지 않음.
- 출력 메타데이터가 mode·선택 RM·출력 형상을 보고.

의도적으로 Ponytail 스타일이다: 작은 HLS top 하나, stdlib만 쓰는 C-sim, 로컬 smoke
테스트에 Vitis 의존성 없음, Vitis HLS/Vitis flow용 HLS pragma는 보존.

## 파일

- `include/dfxisp_accel.hpp` — HLS top 인터페이스, mode/selected-RM enum, 4개 scalar 메타데이터 출력 포인터
- `src/dfxisp_accel.cpp` — checker + baseline core12(demosaic/BLC/WB/CCM, 12-bit) + RM_NORMAL_TONE(gain 1.25x + gamma2.0) + RM_LOW_LIGHT_TONE(2x2 bin + gain 2.0x + gamma2.0)
- `tests/test_dfxisp_csim.cpp` — C-sim smoke 테스트 + golden CSV bit-compare + 아키텍처 불변식 검사
- `tools/gen_golden_vectors.py` — stdlib-only 결정적 golden 생성기(`src/dfxisp_accel.cpp` bit-exact 미러)
- `tools/gen_verification_report.py` — stdlib-only Markdown 검증/리포트 생성기
- `scripts/vitis_hls.tcl` — `dfxisp_accel`용 Vitis HLS 프로젝트 스캐폴드
- `Makefile` — g++ 로컬 C-sim, golden 생성, verify/report, Vitis HLS dry-run 리포트

> 실험 arm(§7)·ablation(§12 Task 5)은 `src/dfxisp_rm.cpp`·`tools/rm_model.py`
> (static / reg_only / dfx_bin / dfx_fp)에 별도로 있다. 현재 스캐폴드의 과거
> post-RGB8 gain/lift 경로는 그 dfx 변종 세트로 이관되어 ablation으로만 남는다.

## `tools/` 파일 상태 (canonical / proxy / legacy, 2026-07-08 Hermes 리뷰 + 같은 날 gamma 재정합)

golden/C-sim/cross-check 경로 자체는 견고하나, canonical golden ↔ SW-eval proxy ↔
legacy/ver0 코드 사이 경계가 문서화되어 있지 않아 혼동 위험이 있었다. 아래 표가 그
경계를 명시한다 — 새 코드는 이 표에 맞춰 어느 범주인지 표시할 것.

> **2026-07-08 같은 날 두 번째 갱신:** Hermes 리뷰(위 표의 최초 버전)와 독립적으로,
> `isp_pipeline_ver1.py`/`newrm_pipeline.py`의 gamma 곡선이 canonical(양쪽 모드
> 공유 gamma-2.0 정수 sqrt LUT)과 다르다는 문제(gamma 2.2/2.5/없음)가 발견되어,
> 이 두 파일은 `tools/archive/`로 이동하고 `baseline_isp_pipeline.py`
> (normal)/`low_light_isp_pipeline.py` (low-light)/`checker.py` (dark-ratio
> checker, 디커플)로 대체됐다. Hermes가 고친 demosaic wrap-around 버그
> (`np.roll` → clamp-to-edge)는 새 파일들에도 동일하게 이식됐다 — 두 수정이
> 서로 다른 실제 버그를 독립적으로 잡았고, rebase 시 병합됐다. 자세한 내용은
> `results/isp-pipeline-recalibration-2026-07-08.md` 참조.

| 파일 | 상태 | 용도 |
|---|---|---|
| `gen_golden_vectors.py` | **canonical golden** | `src/dfxisp_accel.cpp`의 bit-exact 미러 |
| `verify_binning_cross_check.py` | **검증 gate** | binning-demosaic 독립 fuzz 교차검증 (`make verify`에 포함). `low_light_isp_pipeline.py`의 `_bin_demosaic_rggb16`과 교차검증(2026-07-08 이전엔 `isp_pipeline_ver1.py` 대상) |
| `baseline_isp_pipeline.py` | **SW eval proxy (canonical-matched)** | normal arm: BLC+WB+gain(1.25x)+gamma-2.0, gain/gamma까지 `dfxisp_accel.cpp`와 일치하도록 재작성(2026-07-08). `isp_pipeline_ver1.py` 대체 |
| `low_light_isp_pipeline.py` | **SW eval proxy (canonical-matched)** | low-light arm: 2x2 bin-demosaic+BLC+WB+gain(2.0x)+gamma-2.0(normal과 동일 LUT 공유, canonical과 일치). BLC_OFFSET 재보정값은 미확정 상태로 명시(파일 내 주석 참조) |
| `checker.py` | **SW eval proxy (canonical-matched)** | dark-ratio 기반 adaptive 모드 선택기, 두 파이프라인 파일과 독립(상호 import 없음) |
| `newrm_pipeline.py` / `isp_pipeline_ver1.py` | **archived (2026-07-08)** | `tools/archive/`로 이동. gamma가 canonical과 달라(2.2/2.5/없음) 위 3개 파일로 대체됨 — 신규 작업에서 참조 금지, 과거 ablation 계보 참조용으로만 보존 |
| `scheduler_sim.py` / `scheduler_sweep.py` | **정책 시뮬레이션** | hysteresis/temporal/min-dwell 스케줄러 트레이드오프 실험. synthetic luminance 시퀀스 사용 — checker 구현 자체의 검증이 아님 |
| `internal_edge_smoke.py` | **회귀 테스트** | 1x1~8x8 극소/홀수 그리드 스모크 + demosaic 경계 clamp 회귀 테스트 (`make py-verify`). `baseline_isp_pipeline.py`/`checker.py` 양쪽의 독립 demosaic 사본을 각각 검사(2026-07-08 이전엔 `isp_pipeline_ver1.py` 대상) |

## 로컬 C-sim 실행

```bash
cd isppipeline/hls
make csim      # smoke 테스트
make verify    # golden 재생성 + packed RGB888 bit 단위 비교
make report    # reports/latest.md 갱신 (아키텍처 gate 표 포함)
```

`make verify` 예상 출력:

```text
python3 tools/gen_golden_vectors.py --out tests/golden_vectors.csv
wrote tests/golden_vectors.csv (1498 rows including header; 1497 data rows; 9 cases)
./build/dfxisp_csim
DFXISP golden vector compare passed (566 pixels)
DFXISP C-sim smoke tests passed
```

## Golden vector 형식

CSV는 케이스별 메타데이터(mode·threshold·출력 형상·선택 RM)와 입력 RAW 행(`kind=raw`),
기대 출력 행(`kind=rgb`)을 함께 담는다. Policy A(저조도 H/2×W/2)로 인해 입력 픽셀 수와
출력 픽셀 수가 다르므로 두 종류의 행을 분리한다. 커버리지: bright/dark/mixed/
threshold-boundary/bright-recovery/odd-dimension.

## Vitis HLS 스캐폴드 실행

기본값은 ZCU104 파트 `xczu7ev-ffvc1156-2-e`, 5.0 ns 클럭. 설치가 다르면 재정의한다.
`make hls-report`는 `vitis_hls` 설치 없이 top/project/part/clock/소스/예상 출력 경로를 출력한다.

```bash
cd isppipeline/hls
make hls-report                              # dry-run
make hls                                     # 기본 DFXISP_HLS_FLOW=csim
DFXISP_HLS_PART=xczu7ev-ffvc1156-2-e \
DFXISP_HLS_CLOCK=5.0 \
DFXISP_HLS_FLOW=csynth make hls              # C-sim 후 synthesis
```

`vitis_hls`가 `PATH`에 없으면 `make hls`는 안내 메시지와 함께 종료한다.
비표준 경로는 `VITIS_HLS=/path/to/vitis_hls`로 지정한다.

## HLS top 함수

```cpp
extern "C" void dfxisp_accel(
    const uint16_t* raw_bayer,
    uint32_t* rgb_out,             // capacity >= width*height
    int width,
    int height,
    int mode,                      // NORMAL / LOW_LIGHT / AUTO
    uint16_t dark_pixel_threshold, // AUTO: dark 픽셀 비율 > 80%(재보정 2026-07-02) 이면 LOW_LIGHT
    int* out_width,                // 선택된 RM의 출력 폭
    int* out_height,               // 선택된 RM의 출력 높이
    int* selected_mode,            // 해소된 mode (AUTO 해소값)
    int* selected_rm);             // 선택된 tone RM
```
메타데이터가 구조체 포인터 하나가 아니라 **4개의 개별 scalar 출력 포인터**인 이유: 구조체
포인터를 `s_axilite`로 선언하는 방식은 검증된 바 없는(비표준) 패턴이라 adversarial review에서
지적됨(§ 하드웨어/DFX 구조 하단 참조). 개별 scalar 포인터는 Vitis HLS에서 완료 후 read-back
레지스터로 신뢰성 있게 합성되는 정형화된 패턴이다.

## 하드웨어/DFX 구조

`src/dfxisp_accel.cpp`는 로컬 C-sim을 위해 stdlib-only를 유지하면서도 의도한 static/RM
경계를 따라 분할되어 있다:

- `checker_select_mode()` — static-region scene checker. `AUTO`에서 dark-pixel 비율로
  NORMAL/LOW_LIGHT를 결정. 장면 단위 히스테리시스는 시퀀스 스케줄러(RESEARCH §5.2) 담당이며
  단일 프레임 C-sim entry에는 없다.
- `baseline_core12()`/`apply_blc_wb12()` — **shared static** baseline core (ver1).
  BLC + WB(Q8 채널 게인) + CCM(identity)을 **12-bit로 수행**(최종 >>4는 tone에서). **gain/gamma
  없음.** normal 경로는 `demosaic_rggb12()`(RGGB 3x3 Bayer 데모자이크) 결과를 받고, low-light
  경로는 binning-demosaic 결과를 받는다(아래).
- `tone()` — tone RM 스테이지: exposure gain(12-bit) → >>4 → gamma2.0. mode별 gain.
- `run_normal()` — RM_NORMAL_TONE = **gain 1.25× + gamma2.0**. baseline core를 full-res로 실행.
- `run_low_light()`/`compute_binned_rgb_row()` — **RM_LOW_LIGHT_TONE**(DFX reconfigurable
  module 후보). RAW 2x2 **binning-demosaic 융합**(front, R=top-left·G=avg(top-right,
  bottom-left)·B=bottom-right — 채널별 정체성 보존) → baseline core → **gain 2.0× +
  gamma2.0**(back). Vivado DFX 구현에서는 이 tone RM slot을 RM-호환 블록으로 패키징하고,
  checker·baseline core·controller는 static region에 둔다.
- `gamma2()` — γ=2.0을 정수 sqrt `floor(sqrt(255·v))`로 정확히 실현(Python `isqrt`와
  bit-exact). HW에서는 256-엔트리 LUT로 대체 가능.

### 2026-07-02 adversarial-review 수정 (중요)
`/codex:adversarial-review --base 0e433f9`가 두 결함을 발견해 수정했다:
1. **색상 손실 버그(high):** 이전 low-light front-end는 2x2 RGGB 셀의 4개 샘플을
   **하나의 스칼라 평균**으로 합친 뒤 그 값을 다시 Bayer인 것처럼 재-demosaic — 색 정보가
   demosaic 전에 이미 파괴됨. golden 모델(`gen_golden_vectors.py`)이 같은 버그를 그대로
   미러링해서 `make verify`의 bit-exact 테스트가 이를 전혀 못 잡았고, 보고된 lowlight mAP
   증거(`isp_pipeline_ver1.py`)는 **채널 정체성을 보존하는 다른 알고리즘**을 측정한 것이라
   실제 HW 후보의 증거가 아니었음. → `compute_binned_rgb_row()`로 binning+demosaic을 한
   단계에 융합, `_bin_demosaic_rggb16`(SW ver1)과 bit-exact 일치하도록 수정.
2. **메타데이터 RTL 미검증(medium):** `DfxIspResult*` 구조체 포인터를 `s_axilite`로 선언 —
   합성된 RTL에서 실제로 읽을 수 있는지 어떤 산출물로도 확인된 적 없음(cosim도 post-check
   단계에서 실패해 미확인). → 4개 개별 scalar 포인터로 교체(위 HLS top 함수 참조).

**2026-07-02 20:33 KST 갱신:** 이 수정을 반영해 `results/stage4-hw-synthesis-2026-07-02.md`·
`results/stage5-dfx-implementation-2026-07-02.md`를 재합성/재구현하고 수치를 최신화했다
(pr_verify PASS 유지, LUT/FF/DSP 감소 — 버그로 인한 불필요한 2차 demosaic 로직이 제거된
결과). 상세는 두 문서와 `SPEC.md` §10 참조.

C-sim에는 Vitis 전용 헤더가 필요 없다; HLS pragma만 존재하며 로컬 g++ 빌드에서는 무시된다.

## 다음 하드웨어 단계

1. ~~`run_low_light()`의 정적 scratch binning 버퍼를 진짜 streaming line buffer로 교체.~~
   **완료(2026-07-02)** — `row_buf[3][MAX_BINNED_W]` 3-row 슬라이딩 버퍼로 교체, bit-exact 유지.
2. RM_LOW_LIGHT_TONE / RM_NORMAL_TONE을 독립 DFX RM slot 패키징 flow로 승격(§8.3 gate).
3. Policy B(형상보존 upsample/pad)는 DPU가 고정 H×W ABI를 요구할 때만 추가(§4.3).
4. Arm 2(register-only)·Arm 3(DFX) 자원/전력/PR-latency 비교(§7). **Arm2 실측 완료**
   (unified top, C-synthesis) — `results/stage4-hw-synthesis-2026-07-02.md`. Arm1/Arm3와
   전력/PR-latency는 여전히 TODO(Vivado DFX 플로어플랜·구현 필요).

## C-synthesis / Co-sim 실행 노트 (Vitis HLS 2024.1)

과거 worklog와 동일한 두 가지 환경 이슈가 재현된다:

- **source-path 버그:** 중첩된 `build/vitis_hls/...` 프로젝트 경로에서 `add_files`로 design
  source를 추가해도 csim/csynth의 `HLS_SOURCES`에서 누락되어 링크 실패. **우회:** flat
  temp-dir(예: `/tmp/hls_dfxisp/dfxisp_accel/`)에 hpp/cpp/tb를 같은 디렉터리로 복사하고 그
  디렉터리에서 실행.
- **종료-hang:** `close_project` 이후 프로세스가 종료되지 않음(작업 자체는 이미 끝난 상태).
  `timeout -k <grace> <sec> vitis_hls -f run.tcl`로 감싸고 로그의 완료 마커를 확인.
- **cosim `depth=` 필수:** `m_axi` 인터페이스는 co-simulation에 `depth=`가 있어야 한다.
  현재 C-sim fixture 최대(16×16=256px)에 맞춰 `depth=1024`로 설정 — 전체 설계
  envelope(1920×1080)를 그대로 쓰면 cosim의 auto-wrapc 하네스에서 SIGSEGV(추정: 하네스
  내부 스택 할당 오버플로) 발생. 실측 자원/타이밍은 Vitis top 함수 pragma와 무관(합성
  결과에는 영향 없음, cosim 검증 전용 힌트).
