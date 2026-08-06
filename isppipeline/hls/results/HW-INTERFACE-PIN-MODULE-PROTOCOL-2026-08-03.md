<!--
=============================================================================
File   : isppipeline/hls/results/HW-INTERFACE-PIN-MODULE-PROTOCOL-2026-08-03.md
Date   : 2026-08-03 KST
Function: DFXISP 하드웨어 인터페이스(핀 매핑 / 모듈-로직 관계 / 통신 프로토콜)를
          한 문서로 브리핑하기 위한 정본 프롬프트. 다른 세션/에이전트/리뷰어에게
          이 레포의 HW 경계를 처음부터 설명할 때 그대로 붙여넣어 쓸 수 있도록
          작성. ASCII 다이어그램 포함.
Audience: 이 레포의 HW 트랙(HLS/Vivado DFX)을 처음 보는 사람 또는 AI 세션.
          "0. 결론부터"만 읽어도 큰 그림이 잡히고, 표/다이어그램은 근거를
          검증하고 싶을 때 참조.
Sources : isppipeline/hls/reports/csynth/dfxisp_accel_ver1_csynth.rpt(실측 RTL
          포트, Interface Summary), isppipeline/hls/results/
          dfxisp-accel-connectivity-2026-07-03.md(top-level 434 wire 전수
          연결표, 이 문서가 그 위에 얹는 요약), isppipeline/hls/src/
          dfxisp_accel.cpp(pragma/함수 구조 정본), SPEC.md §6-7, results/
          dfx-vivado-considerations-2026-07-03.md, results/design-limitations-
          2026-07-03.md, results/dfx-reimplementation-2026-08-01.md.
Note    : 이 프로젝트는 "HW 수치 위조 금지"(SPEC.md §11.4) 원칙을 따른다 --
          아래 수치는 전부 위 소스 문서/리포트에서 실측·인용했고, 확인 안 된
          값(예: 정확한 axilite 레지스터 바이트 오프셋)은 "미확인/TODO"로
          명시했지 추정치를 사실처럼 적지 않았다.
=============================================================================
-->
# DFXISP 하드웨어 인터페이스 정본 — 핀 매핑 · 모듈 관계 · 통신 프로토콜 (2026-08-03)

## 0. 결론부터

DFXISP는 **fabric-only 단계**(Stage 4~5 완료, Stage 6 보드 미착수)의 Zynq
UltraScale+ ZCU104(`xczu7ev-ffvc1156-2-e`) DFX 설계다. 따라서 "핀 매핑"은 두
층위로 나눠 이해해야 한다.

1. **HLS top 함수 ↔ RTL 포트 ↔ AXI 버스 핀 매핑** — 이건 **실측·확정**돼 있다
   (Vitis HLS 2024.1 csynth 결과, `dfxisp_accel_ver1_csynth.rpt`). 아래 §2가
   정본.
2. **ZCU104 보드 물리 핀(BGA ball) 매핑** — **아직 존재하지 않는다.** PS/DDR
   통합, XDC 제약, 실제 I/O 핀 할당은 Stage 6(보드 실장, 미착수) 영역이다.
   지금까지의 DFX 구현(`pr_verify` PASS)은 **PL fabric 내부**(config1/
   config2 pblock, partition pin)까지만 다뤘고, 칩 바깥으로 나가는 핀은
   fabric-only 특성화를 위해 대부분 tie-off했다(§4.4, §6).

**모듈 관계**의 핵심은 *shared baseline core + 상호배타 tone RM slot*
아키텍처(SPEC.md §0)다: `checker`가 모드를 정하고, `run_normal`/
`run_low_light` 중 **정확히 하나만** `ap_start`된다 — 같은 이유로 이 둘은
gmem1(쓰기) 버스를 실제 충돌 없이 공유할 수 있다(§3.2).

**통신 프로토콜**은 표준 Xilinx/AMD IP 관례를 그대로 따른다: 대용량 프레임
버퍼(`raw_bayer`/`rgb_out`)는 **AXI4 memory-mapped master**(`m_axi`,
번들 `gmem0`/`gmem1`), 스칼라 인자·제어·메타데이터 read-back은 **AXI4-Lite
slave**(`s_axilite`, 번들 `control`)다. **DFX 재구성 프로토콜(ICAP)은
설계에 아직 없다** — `ICAPE3`/`STARTUPE3` 사용 개수 실측 0개(§4.4)이고, PR
컨트롤러 자체가 미합성 상태다. 이게 이 프로젝트에서 유일하게 진짜
blocking인 항목(`results/improvement-strategy-2026-07-03.md`).

---

## 1. 시스템 레벨 데이터 흐름 (알고리즘 관점)

```text
┌──────────────┐    ┌─────────────────┐    ┌───────────────────────────┐    ┌────────────┐
│ 입력 RAW      │───▶│ ① Scene checker  │───▶│ ② 상호배타 tone RM slot    │───▶│ RGB32 출력  │
│ pseudo/real   │    │ (static region)  │    │  (재구성 가능, RP)         │    │ + 메타데이터│
│ Bayer RGGB    │    │ dark_ratio 비교  │    │                            │    │            │
└──────────────┘    └─────────────────┘    └───────────────────────────┘    └────────────┘
                            │                      │
                     selected_mode           NORMAL: run_normal
                     (0=NORMAL/1=LOW_LIGHT)         └─ demosaic→baseline_core12→gain1.25×→γ2.0
                            │                  LOW_LIGHT: run_low_light
                            │                        └─ 2x2 bin-demosaic→baseline_core12(BLC=32)
                            │                           →gain2.0×→γ2.0, 출력 H/2×W/2
                            ▼
                 selected_rm(0/1), out_width, out_height  ── s_axilite read-back 레지스터로 노출
```

baseline core(demosaic+BLC+WB+CCM)는 두 경로가 **공유**하는 함수
`apply_blc_wb12()`이고, gain/gamma는 tone RM에만 존재한다(중복 없음, C-sim
게이트로 검증됨 — SPEC.md §8 "아키텍처 gate 6종 PASS").

---

## 2. HLS Top 함수 ↔ 핀 매핑

### 2.1 C 인터페이스 → AXI 번들 매핑 (소스 pragma, `dfxisp_accel.cpp:349-424`)

세 개의 top 함수가 있고 셋 다 같은 번들링 규칙을 쓴다 — `dfxisp_accel`(통합,
런타임 mode-select, DFX 없음 = **Arm2**), `rm_normal_tone_top` /
`rm_low_light_tone_top`(독립 top, DFX RP 후보 = **Arm3**의 RM 소스).

| C 인자 | 방향 | HLS INTERFACE pragma | AXI 번들/역할 |
|---|---|---|---|
| `raw_bayer` (`const uint16_t*`) | in | `m_axi bundle=gmem0 offset=slave` + `s_axilite bundle=control` | 데이터: gmem0(AXI4 master, 버스트 read). 포인터 주소값 자체는 control(AXI4-Lite) 레지스터로 프로그램 |
| `rgb_out` (`uint32_t*`) | out | `m_axi bundle=gmem1 offset=slave` + `s_axilite bundle=control` | 데이터: gmem1(AXI4 master, 버스트 write). 주소값은 control 레지스터 |
| `width`, `height` (`int`) | in | `s_axilite bundle=control` | 스칼라 레지스터 |
| `mode` (`int`, `dfxisp_accel`만) | in | `s_axilite bundle=control` | 스칼라 레지스터, `DfxIspMode` |
| `dark_pixel_threshold` (`uint16_t`, `dfxisp_accel`만) | in | `s_axilite bundle=control` | 스칼라 레지스터, checker 임계 |
| `out_width`, `out_height` (`int*`) | out | `s_axilite bundle=control` | **포인터지만 m_axi 아님** — HLS가 내부 값을 latch해 read-back 레지스터로 노출(구조체 포인터 방식은 adversarial review로 폐기, SPEC.md §5.3) |
| `selected_mode`, `selected_rm` (`int*`, `dfxisp_accel`만) | out | `s_axilite bundle=control` | 동일 read-back 패턴 |
| `return` | — | `s_axilite bundle=control` | `ap_start`/`ap_done`/`ap_idle`/`ap_ready` 제어 레지스터 |

**DFX 계약(중요):** `rm_normal_tone_top`과 `rm_low_light_tone_top`은 인자
타입·순서·개수(`raw_bayer, rgb_out, width, height, out_width, out_height` —
6개)가 **정확히 동일**하다. DFX가 하나의 Reconfigurable Partition(RP) 슬롯에
서로 다른 RM(Reconfigurable Module)을 꽂으려면 포트 시그니처가 같아야 하기
때문(RESEARCH.md §2.3 "동일 downstream 인터페이스 계약"). `dfxisp_accel`은
이 둘을 **런타임 분기**로 내부에 모두 담은 register-only 버전(Arm2)이며 DFX
RP 경계가 없다.

### 2.2 실측 RTL 포트 (Vitis HLS 2024.1 csynth, `dfxisp_accel_ver1_csynth.rpt` Interface Summary)

`dfxisp_accel` top 기준, **RTL 레벨에서 실제로 나온 핀**(발췌 — 전체는 리포트
원본 참조):

```text
[control, AXI4-Lite slave — bundle "control", 17 signals]
  s_axi_control_AWVALID   in  1   s_axi_control_AWREADY  out 1
  s_axi_control_AWADDR    in  7   s_axi_control_WVALID   in  1
  s_axi_control_WREADY    out 1   s_axi_control_WDATA    in  32
  s_axi_control_WSTRB     in  4   s_axi_control_ARVALID  in  1
  s_axi_control_ARREADY   out 1   s_axi_control_ARADDR   in  7
  s_axi_control_RVALID    out 1   s_axi_control_RREADY   in  1
  s_axi_control_RDATA     out 32  s_axi_control_RRESP    out 2
  s_axi_control_BVALID    out 1   s_axi_control_BREADY   in  1
  s_axi_control_BRESP     out 2

[제어/클럭, ap_ctrl_hs — 3 signals]
  ap_clk       in  1        ap_rst_n     in  1        interrupt   out  1

[gmem0, AXI4 master — bundle "gmem0", raw_bayer 버스트 read, 49 signals]
  m_axi_gmem0_{AW,W,AR,R,B}* 풀 AXI4 채널(AWADDR/ARADDR **64-bit**,
  AWLEN/ARLEN 8-bit(버스트 최대 256), AWSIZE/ARSIZE 3-bit, ID/USER/CACHE/
  PROT/QOS/REGION 포함)

[gmem1, AXI4 master — bundle "gmem1", rgb_out 버스트 write, 동일 49 signals]
```

핵심 사실(실측):
- `s_axi_control_AWADDR`/`ARADDR` 폭 = **7 bit** → control 레지스터 주소
  공간은 **128 byte**(워드 정렬 시 최대 32개의 32-bit 레지스터).
- `m_axi_gmem{0,1}_AW/ARADDR` 폭 = **64 bit** → `raw_bayer`/`rgb_out`
  포인터는 control 레지스터 안에서 저위/고위 32-bit 두 칸으로 프로그램되는
  표준 Vitis HLS 64-bit 포인터 관례를 따른다(정확한 오프셋은 §2.3 참조).
- `interrupt` 출력 핀이 있다 → 표준 Vitis HLS 템플릿상 GIER/IP_IER/IP_ISR
  레지스터가 control 맵에 존재한다는 뜻(§2.3).

### 2.3 Control(AXI4-Lite) 레지스터 맵

**주의:** 아래는 Vitis HLS가 `interrupt` 포트 + 다중 스칼라/포인터 인자
조합에서 생성하는 **표준 템플릿 레이아웃**이다. 이 레포에는 합성 생성물인
`dfxisp_accel_control_s_axi.v`/`xdfxisp_accel_hw.h`가 커밋돼 있지 않아
**정확한 바이트 오프셋은 미확인(TODO)** — 재합성 후 해당 파일에서 직접
확인해야 한다. 레지스터가 **어떤 신호를 담는지**는
`dfxisp-accel-connectivity-2026-07-03.md`의 `control_s_axi`(39 포트) 표에서
실측 확인됨(§3.2 인용).

```text
0x00  ap_control   [0]=ap_start [1]=ap_done [2]=ap_idle [3]=ap_ready
                    [7]=auto_restart  (표준 ap_ctrl_hs 레이아웃)
0x04  GIER         global interrupt enable
0x08  IP_IER        인터럽트 enable (ap_done/ap_ready)
0x0c  IP_ISR        인터럽트 status (r/w1c)
0x10..  raw_bayer_addr  (64-bit, lo/hi 두 워드)
      rgb_out_addr    (64-bit, lo/hi 두 워드)
      width, height, mode, dark_pixel_threshold   (32-bit 스칼라, 각 1워드)
      out_width, out_height, selected_mode, selected_rm  (read-only,
        `_ap_vld` 플래그와 함께 노출 — 값이 유효해지는 시점을 vld로 판별)
```

프로그래밍 순서(표준 Vitis HLS control-flow, `ap_start`는 idle일 때만
write): `raw_bayer_addr`/`rgb_out_addr`/스칼라 인자 write →
`ap_control.ap_start=1` write → (polling: `ap_control.ap_done` 를 때까지
read, 또는 `interrupt` 사용) → `out_*`/`selected_*` read.

### 2.4 ASCII: Top 블록 핀 그룹 다이어그램

```text
                         ┌────────────────────────────────────┐
                         │        dfxisp_accel (top)           │
                         │        (Arm2, 통합 register-only)    │
   ap_clk ───────────────▶│ ap_clk                              │
   ap_rst_n ──────────────▶│ ap_rst_n                            │
                         │                                      │◀── interrupt
   ┌─────────────────────▶│ s_axi_control  (AXI4-Lite, 32-bit,  │
   │  AXI-Lite master     │   AWADDR/ARADDR 7-bit = 128B space) │
   │  (PS/AXI Interconnect│   raw_bayer_addr / rgb_out_addr /   │
   │   또는 테스트벤치)     │   width / height / mode /            │
   └─────────────────────▶│   dark_pixel_threshold /             │
                         │   out_width / out_height /           │
                         │   selected_mode / selected_rm         │
                         │   (전부 read-back 가능)                │
                         │                                      │
   DDR/BRAM (raw_bayer)◀─▶│ m_axi_gmem0  (AXI4 master, 64-bit   │
                         │   addr, burst read, ID/CACHE/QOS 포함)│
                         │                                      │
   DDR/BRAM (rgb_out) ◀─▶│ m_axi_gmem1  (AXI4 master, 64-bit    │
                         │   addr, burst write, ID/CACHE/QOS)    │
                         └────────────────────────────────────┘
```

---

## 3. 모듈 / 로직 계층 관계

### 3.1 소스 레벨 호출 그래프 (`dfxisp_accel.cpp`, 함수 정의 순서와 일치)

```text
dfxisp_accel(raw_bayer, rgb_out, width, height, mode, dark_pixel_threshold,
             out_width, out_height, selected_mode, selected_rm)
  │
  ├─▶ checker_select_mode(raw, width, height, mode, dark_pixel_threshold)
  │     └─▶ sample_clamped()  // RAW 도메인 dark-pixel 카운트 (LOOP_TRIPCOUNT
  │                           //   min=16 max=2073600, 스캔 파이프라인)
  │
  ├─▶ [selected == LOW_LIGHT] run_low_light(raw, rgb_out, width, height, &ow, &oh)
  │     ├─▶ compute_binned_rgb_row()   // 2x2 RAW binning-demosaic (fused,
  │     │                              //   채널 정체성 보존, R=TL/G=avg/B=BR)
  │     ├─▶ apply_blc_wb12(..., blc_offset=32)  // baseline core, 공유 함수
  │     └─▶ tone(v12, 2, 1) → gamma2()          // gain 2.0x + γ2.0
  │
  └─▶ [else] run_normal(raw, rgb_out, width, height)
        ├─▶ demosaic_rggb12()
        ├─▶ baseline_core12() ─▶ apply_blc_wb12(..., blc_offset=32)  // 같은
        │                        // 공유 함수, low-light와 BLC_OFFSET12만 다름
        └─▶ tone(v12, 5, 4) → gamma2()          // gain 1.25x + γ2.0
```

`apply_blc_wb12()`는 **한 개의 함수**로 정의돼 있고 두 경로가 그대로
호출한다 — "shared baseline ISP core" 원칙(README.md 핵심 원칙 1)이 소스
레벨에서 문자 그대로 지켜지는 지점. `gamma2()`는 런타임 정수 sqrt가 아니라
256-entry ROM LUT(`GAMMA2_LUT`)로 구현돼 있다(자원 5배 절감 사전 최적화,
SPEC.md §10 참고 각주).

### 3.2 합성된 RTL 인스턴스와 버스 공유 관계

`dfxisp_accel`은 csynth 후 **top 레벨에서 6개의 서브모듈 인스턴스**로
쪼개진다(`dfxisp-accel-connectivity-2026-07-03.md`, 434 signal wire 전수
추출 — 아래는 그 문서의 요약을 인용):

| 인스턴스 | 역할 | 포트 수 |
|---|---|---:|
| `PIPE151` | checker/dark-scan 파이프라인 | 57 |
| `run_normal` | NORMAL 경로 전체(demosaic+BLC+WB+tone) | 102 |
| `run_low_light` | LOW_LIGHT 경로 전체(bin-demosaic+BLC+WB+tone) | 104 |
| `control_s_axi` | AXI4-Lite 레지스터 파일 | 39 |
| `gmem0_m_axi` | raw_bayer AXI4 read 어댑터 | 66 |
| `gmem1_m_axi` | rgb_out AXI4 write 어댑터 | 66 |

**버스 공유(중요한 구조적 사실, connectivity 표 실측):**
- **gmem0(읽기)**는 `PIPE151`/`run_normal`/`run_low_light` **셋이 공유**한다
  (`gmem0_ARREADY`/`RVALID`/`RDATA`/`RFIFONUM`가 세 인스턴스에 동일하게
  팬아웃) — checker와 실제 처리 경로가 각자 `raw_bayer`를 다시 읽기 때문.
- **gmem1(쓰기)**는 `run_normal`/`run_low_light` **둘만 공유**한다. 이 둘은
  상호배타적으로 하나만 `ap_start`되므로(§3.3) 실제 버스 충돌은 없다 —
  **DFX로 RM을 물리적으로 교체해도 이 공유 토폴로지는 그대로 유지되는 게
  RP 설계의 전제**다.
- `control_s_axi`가 내보내는 값들은 top의 `always` 블록에서
  `raw_bayer_read_reg_457` 같은 이름으로 한 번 래치된 뒤 서브모듈에
  전달된다(레지스터 슬라이스 — 타이밍 분리 목적으로 추정, 문서에 근거
  코멘트 있음).

### 3.3 DFX RP/RM 계약 — static region vs RM slot

```text
┌───────────────────────────── static region (고정, PR 대상 아님) ─────────────────────────────┐
│                                                                                              │
│   s_axi_control ──▶ [checker/mode FSM (PIPE151)] ──▶ selected_mode ──┐                        │
│         │                                                            │                        │
│   m_axi gmem0 ◀──── raw_bayer 공유 read 채널 ─────────────────────────┤                        │
│         │                                                            ▼                        │
│         │                                          ┌──────────────────────────────┐          │
│         │                                          │   RM slot (Reconfigurable    │          │
│         │                                          │   Partition, 상호배타)        │          │
│         └─────────────────────────────────────────▶│                              │          │
│                                                     │  RM_NORMAL_TONE   ◀OR▶       │          │
│   m_axi gmem1 ◀──── rgb_out 공유 write 채널 ─────────│  RM_LOW_LIGHT_TONE          │          │
│         │                                          │  (port 시그니처 동일,        │          │
│         │                                          │   partition pin 3개로 경계)  │          │
│         ▼                                          └──────────────────────────────┘          │
│   out_width/out_height/selected_mode/selected_rm  ◀── 메타데이터 read-back ────────────────────│
│                                                                                              │
│   [DFX/PR 컨트롤러]  ※ 현재 미합성 — §4.4 참조                                                  │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

- static region: AXI/제어 wrapper, checker/mode FSM, (unified top에선) 두
  경로 모두 상주. Arm3(DFX)에서는 baseline core + tone 계산 중
  **RM_NORMAL_TONE/RM_LOW_LIGHT_TONE만** 재구성 대상이고 나머지는 static.
- RM slot: `pr_verify` PASS 실측(2026-08-01 재구현) 기준 **partition pin
  3개** — 이 경계를 넘는 신호가 3개뿐이라는 뜻. 과거 2개→15개→3개로
  변한 이력이 있고(§4.4), 15→3 감소의 **근본 원인은 아직 조사되지 않은
  open item**(`design-limitations-2026-07-03.md` §4.5) — 기능적으로는
  `pr_verify` PASS라 문제 없지만, "왜 신호 개수가 이렇게 변하는지"는
  정직하게 미확인으로 남겨야 한다.

---

## 4. 통신 프로토콜

### 4.1 AXI4 memory-mapped master (`m_axi`, 번들 `gmem0`/`gmem1`)

- **역할:** 대용량 프레임 버퍼(`raw_bayer` 최대 W×H uint16, `rgb_out` 최대
  W×H uint32)를 DDR(또는 cosim BFM 메모리)과 주고받는다.
- **채널 5개**(표준 AXI4): AW(주소+제어, write) / W(데이터, write) / B(응답,
  write) / AR(주소+제어, read) / R(데이터+응답, read).
- **버스트:** `AWLEN`/`ARLEN` 8-bit → 버스트 길이 최대 256 전송, `AWSIZE`/
  `ARSIZE`로 전송 폭(gmem0=16-bit 워드, gmem1=32-bit 워드) 지정.
- **ID/QOS/CACHE/PROT/REGION/USER**: 인터커넥트/DDR 컨트롤러용 표준
  사이드밴드 신호, HLS가 기본값으로 생성(대부분 이 fabric-only
  characterization에서는 tie-off 또는 고정값).
- **depth= 힌트**: `#pragma HLS INTERFACE m_axi ... depth=2048`은 **cosim
  BFM 메모리 모델 크기 힌트**일 뿐 합성된 RTL 동작에는 영향 없음(실제
  깊이는 런타임 `width*height`) — 소스 코드 주석에 명시된 실전 트러블슈팅
  기록(cosim SIGSEGV 회피 이력).

### 4.2 AXI4-Lite slave (`s_axilite`, 번들 `control`)

- **역할:** PS(또는 테스트벤치)가 스칼라 인자를 쓰고, 시작/완료를 제어하고,
  메타데이터를 읽어오는 경량 레지스터 인터페이스. 버스트 없음, 항상 단일
  32-bit 전송.
- **채널 5개**: AW/W/B(쓰기), AR/R(읽기) — 주소 폭만 7-bit로 좁다(§2.2).
- **`ap_ctrl_hs` 프로토콜**: `ap_start`(레벨, PS가 세팅) → 코어 동작 →
  `ap_done`(펄스/레벨, 완료 시 세팅되고 `ap_start`/`ap_continue`에 따라
  전략 다름) → `ap_idle`(재시작 가능 여부) → `ap_ready`(다음 트랜잭션
  받을 준비). `interrupt` 출력이 있어 폴링 대신 인터럽트 기반 완료 감지도
  가능(GIER/IER/ISR, §2.3).

### 4.3 ASCII: 핸드셰이크 타이밍

**AXI4-Lite write (레지스터 1개 쓰기, 예: `mode` 설정):**

```text
clk      __|‾|__|‾|__|‾|__|‾|__|‾|__
AWVALID  ___/‾‾‾‾‾‾‾\___________________   AWADDR = mode 오프셋
AWREADY  _______/‾‾‾‾‾\_________________
WVALID   ___/‾‾‾‾‾‾‾\___________________   WDATA  = mode 값, WSTRB=4'hF
WREADY   _______/‾‾‾‾‾\_________________
BVALID   ___________/‾‾‾‾‾\_____________   BRESP  = OKAY(2'b00)
BREADY   _________/‾‾‾‾‾‾‾‾‾\___________
```

**AXI4 burst read (gmem0에서 `raw_bayer` 한 줄 읽기):**

```text
clk      __|‾|__|‾|__|‾|__|‾|__|‾|__|‾|__
ARVALID  ___/‾‾‾\_______________________   ARADDR=base, ARLEN=N-1, ARSIZE=2B
ARREADY  _____/‾‾‾\_____________________
RVALID   _________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\_____   N개 데이터 비트(RDATA) 연속 전송
RREADY   _______/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\___
RLAST    ________________________/‾\____   마지막 전송에서만 1
```

### 4.4 DFX 재구성 프로토콜 — 현재 상태: **설계에 없음**

이론적으로 DFX 부분 재구성은 `STARTUPE3`(재구성 트리거/완료 신호 관장)와
`ICAPE3`(partial bitstream을 fabric에 스트리밍하는 설정 포트)를 통해
이뤄진다. **실측(`dfx-vivado-considerations-2026-07-03.md` §4.1):**

```text
ICAPE3 사용 개수 (config1)  : 0 / 가용 2   ← 실측
ICAPE3 사용 개수 (config2)  : 0 / 가용 2   ← 실측
STARTUPE3 사용 개수 (양쪽)  : 0 / 가용 1   ← 실측
```

즉 지금까지 완주한 것은 **`pr_verify` PASS + partial bitstream 생성**
(fabric netlist 레벨의 정합성 증명)이지, **누가 언제 재구성을 트리거하고
실제로 ICAP에 bitstream을 흘리는지의 실행 경로는 아직 없다.** 이게
`results/improvement-strategy-2026-07-03.md`가 "유일한 진짜 blocking
item"으로 지목한 항목이다. 1차 FSM 설계(`results/pr_controller/
pr_controller.v`)와 word-count 기반 완료 판정 시뮬레이션은
있으나(`results/phase0-2-execution-2026-07-03.md`), **아직 top 설계에
통합/합성되지 않았다.**

**ASCII: 재구성 시퀀스(이론적 분해, `pr-latency-breakdown-2026-07-02.md`
기준 — 보드 미실측, 스펙 유도값)**

```text
[정상 동작]                [drain]        [ICAP 전송]              [warm-up]   [정상 동작 재개]
selected_rm 전환 트리거 ──▶ 진행중 트랜   ──▶ partial bitstream    ──▶ RM 클럭  ──▶ 새 RM으로
(checker 히스테리시스,       잭션 완료      1,447,424B(신규pblock)     안정화       처리 재개
 장면 단위)                 대기(74~171     을 ICAPE3로 스트리밍
                            cycle, 측정)    peak 3.62ms/전형 14.47ms
                                            (2026-07-03 재floorplan
                                             후 재계산, >99.9% 비중)
```

30fps 프레임 예산(33.3ms) 대비 peak 재구성 지연 비중은 계산 가능하지만,
**드라이버/FSM 오버헤드는 PR 컨트롤러가 없어 계산 불가**(TODO, 보드
필요) — 위 수치에 그 오버헤드는 포함돼 있지 않다는 점을 명시해야 한다.

### 4.5 partition pin 이력 (RM slot 경계 신호 수)

| 시점 | partition pin 수 | 비고 |
|---|---:|---|
| 최초(2026-07-02, 구 floorplan) | 2 | 구조체 포인터 메타데이터 시절 — write-back 미검증 이슈와 연결 |
| 4개 scalar 출력 포인터로 교체 후 | 15 | "물리적으로 실제 존재하는 신호"라는 사후 증거로 해석(`stage5-dfx-implementation-2026-07-02.md`) |
| pblock 재floorplan(X1Y0:X2Y0) 후 | **3** | 현재값. pr_verify PASS 유지, 하지만 **15→3 감소의 원인 미조사**(open item) |

---

## 5. 클럭 / 리셋 도메인

```text
ap_clk ────┬─▶ dfxisp_accel (top, ap_ctrl_hs)
           ├─▶ control_s_axi (ACLK)
           ├─▶ gmem0_m_axi / gmem1_m_axi (ACLK)
           └─▶ PIPE151 / run_normal / run_low_light (모두 동일 ap_clk)

ap_rst_n ──▶ ap_rst_n_inv(내부 반전, active-low→active-high 변환) ──▶
             control_s_axi.ARESET / gmem{0,1}_m_axi.ARESET /
             PIPE151.ap_rst / run_normal.ap_rst / run_low_light.ap_rst
```

**단일 클럭 도메인**(200MHz 목표, 5.0ns 제약) — 별도 CDC(clock-domain
crossing) 로직은 이 설계에 없다. 클럭 인에이블(`ACLK_EN`)은 모든 서브모듈에서
`1'b1` tie-off(항상 인에이블).

---

## 6. 현재 상태 · Gap 요약

| 항목 | 상태 | 근거 |
|---|---|---|
| HLS top ↔ AXI 핀 매핑(§2) | ✅ 실측 확정 | csynth Interface Summary |
| top-level 6개 인스턴스 434-wire 연결(§3.2) | ✅ 실측(Verilog 파싱) | connectivity-2026-07-03.md |
| control 레지스터 정확한 바이트 오프셋(§2.3) | 🟡 레이아웃 종류는 알지만 오프셋 미확인 | 생성 헤더 미커밋 — 재합성 시 확인 필요 |
| RM slot 상호배타 게이트 | ✅ C-sim 아키텍처 gate PASS | SPEC.md §8 |
| partition pin 3개(현재) | ✅ 실측(`pr_verify`) | dfx-reimplementation-2026-08-01.md |
| partition pin 15→3 원인 | ⬜ 미조사 | design-limitations-2026-07-03.md §4.5 |
| ICAP/STARTUPE3 재구성 프로토콜 | ⬜ **미구현**(0개 인스턴스) | dfx-vivado-considerations §4.1 |
| PR 컨트롤러 통합 | ⬜ 1차 FSM만 존재, top 미통합 | improvement-strategy §"blocking item" |
| ZCU104 보드 물리 핀(BGA) 매핑 | ⬜ **아직 존재하지 않음**(Stage 6 미착수) | README.md "Current status" |
| PS/DDR 통합 | ⬜ 미착수 | SPEC.md §7 "PS/DDR 미통합" |

---

## 7. 참고 문서

- `isppipeline/hls/results/dfxisp-accel-connectivity-2026-07-03.md` — 434 wire
  전수 연결표(이 문서 §2~3의 1차 소스)
- `isppipeline/hls/reports/csynth/dfxisp_accel_ver1_csynth.rpt` — 실측 RTL
  포트 Interface Summary
- `isppipeline/hls/src/dfxisp_accel.cpp` — top 함수/pragma/호출그래프 정본
- `SPEC.md` §6(인터페이스), §7(HW/DFX 사양), §11(제약·알려진 이슈)
- `results/dfx-vivado-considerations-2026-07-03.md` — I/O 핀 초과 이슈,
  ICAPE3/STARTUPE3 실측, DRC 우회 체크리스트
- `results/design-limitations-2026-07-03.md` §4.5 — partition pin 15→3 미조사
- `results/dfx-reimplementation-2026-08-01.md` — 최신(2026-08-01) DFX
  fabric-only 재구현, partition pin 3 재확인
- `results/pr-latency-breakdown-2026-07-02.md` — 재구성 지연 이론적 분해
- `results/improvement-strategy-2026-07-03.md` — PR 컨트롤러 부재가 유일한
  진짜 blocking item이라는 결론

문서 끝 — 실제 재합성/재구현 시 §2.3(레지스터 오프셋)과 §4.5(partition pin
원인)를 채우는 것이 다음으로 유용한 후속 작업.
