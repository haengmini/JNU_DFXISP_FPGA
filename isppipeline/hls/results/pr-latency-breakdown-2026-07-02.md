<!--
=============================================================================
File   : isppipeline/hls/results/pr-latency-breakdown-2026-07-02.md
Date   : 2026-07-02
Time   : 23:20 KST
Function: DFX 재구성(Reconfiguration) latency를 구성 단계별로 분해 -- 실측 가능한
          항목(파이프라인 drain/warm-up, partial bitstream 크기)과, 실측 불가능해
          공개 데이터시트 스펙으로 이론적 상한/전형값을 계산한 항목(ICAP 전송)을
          명확히 구분해 기록한다. 실제 ICAP 트랜잭션·FSM 오버헤드는 보드 전용.
Goal   : "DFX 전환 될 때 걸리는 latency를 단계별로 모두 측정해서 기록해" 요청에 대한
         응답 -- 지어내지 않고(SPEC.md §11 "HW 수치 위조 금지"), 측정된 것/스펙에서
         유도한 것/보드에서만 확인 가능한 것을 항목별로 정직하게 분리했다.
=============================================================================
-->
# DFX 재구성 Latency 단계별 분해 (2026-07-02)

> **읽기 전 주의:** 이 문서의 수치는 **세 가지 서로 다른 신뢰도**를 가진다.
> (①측정) 파이프라인 drain/warm-up 사이클과 partial bitstream 크기는 오늘
> 재합성/재구현한 실제 산출물에서 나온 값. (②스펙 유도) ICAP 전송 시간은 AMD
> UG570(Zynq UltraScale+ Configuration)의 ICAPE3 대역폭 스펙(공개 데이터시트)에
> partial bitstream 실측 크기를 나눈 **이론적 계산값**이며, 이 보드에서 직접
> 측정한 값이 아니다. (③TODO) ICAP 드라이버/FSM 오버헤드, PS-PL 핸드셰이크,
> 실제 재구성 후 첫 프레임 지연은 **PR 컨트롤러를 아직 만들지 않아 계산조차
> 불가능**하며 실보드에서만 확인된다. 세 카테고리를 표에서 "근거" 열로 항상 구분.
>
> **갱신(23:10 KST):** 이 계산값보다 더 정확한 근거를 얻기 위해 Vivado(XSIM)로
> 실제 `rm_lowlight_partial.bit`를 ICAPE3 UNISIM 동작 모델에 직접 흘리는 시뮬레이션을
> 시도했다 — SYNC는 성공했지만 trigger→완료 신호는 격리된 테스트벤치에서 끝내 얻지
> 못했다(3가지 독립 방법 모두 동일 결론, 원인 분석 포함). 그 과정에서 payload가
> 정확히 686,532 bytes(171,633 word, 헤더 내장 길이 필드와 일치)임을 파일에서 직접
> 검증했다 — §2의 계산은 이 정밀한 word 수를 반영해 갱신했다. 상세:
> `results/pr-latency-vivado-sim-2026-07-02.md`.

## 1. 재구성 이벤트의 정의

RESEARCH/SPEC의 스케줄러 설계(Stage 1)에 따라 **RM 전환은 프레임 단위가 아니라
장면(scene) 단위**로 일어난다(narrow 히스테리시스 + temporal_N=3, mismatch 0.015).
한 번의 전환 = ① 진행 중인 old-RM 파이프라인 drain → ② ICAP를 통한 partial
bitstream 로드 → ③ new-RM 파이프라인 warm-up(첫 유효 출력까지) 3단계로 분해한다.

```text
... frame(old RM) | DRAIN | ICAP LOAD (partial bitstream) | WARM-UP | frame(new RM) ...
    steady-state   <-171~or~74 cyc->  <-- 686,532 B payload (171,633 word) -->  <-74~or~171 cyc->  steady-state
```

## 2. 단계별 수치

| 단계 | 근거 | RM_NORMAL_TONE | RM_LOW_LIGHT_TONE |
|---|---|---|---|
| ① Drain (진행 중인 old-RM 파이프라인 flush) | **① 측정** — Vitis HLS csynth 최소 latency(오늘 재합성), target clock 5.0ns | 171 cycles = **0.855 µs** | 74 cycles = **0.370 µs** |
| ② ICAP 전송 (partial bitstream 로드) — 이론적 peak | **② 스펙 유도** — payload 686,532 B(헤더 제외, 171,633 word — Vivado 시뮬레이션으로 파일에서 직접 검증, `pr-latency-vivado-sim-2026-07-02.md`) ÷ ICAPE3 peak 400 MB/s(100 MHz × 32-bit, AMD UG570) | **1.716 ms** | **1.716 ms**(동일 크기) |
| ② ICAP 전송 — 보수적/전형값 | **② 스펙 유도** — 동일 payload ÷ 100 MB/s(PR 문헌에서 흔히 인용되는 드라이버/오버헤드 포함 실효 대역폭 하한) | **6.865 ms** | **6.865 ms** |
| ③ Warm-up (new-RM 파이프라인 채움, 첫 유효 출력까지) | **① 측정** — 위와 동일 latency 수치를 new-RM 기준으로 적용 | 74 cycles = **0.370 µs**(LOW_LIGHT→NORMAL 전환 시) | 171 cycles = **0.855 µs**(NORMAL→LOW_LIGHT 전환 시) |
| ④ ICAP 드라이버/FSM 오버헤드, PS↔PL 핸드셰이크 | **③ TODO** — PR 컨트롤러 미합성. 계산 근거 없음 | TODO(보드) | TODO(보드) |
| **합계 (①+②+③, ④ 제외)** | 혼합 | **peak 1.718 ms / 전형 6.867 ms** | **peak 1.718 ms / 전형 6.867 ms** |

## 3. 해석

1. **ICAP 전송이 압도적으로 지배적이다.** Drain(≤0.855 µs)과 warm-up(≤0.855 µs)을
   합쳐도 수 µs 수준인데, ICAP 전송은 peak 가정에서도 1.7 ms — **약 1,000배** 차이.
   즉 이 설계에서 재구성 latency 예산은 사실상 **"partial bitstream 크기 ÷ ICAP
   대역폭"** 한 항으로 근사해도 무방하다(drain/warm-up은 예산에서 반올림 오차 수준).
2. **partial bitstream 크기가 두 RM 모두 동일**(686,532 B payload, 171,633 word 모두
   일치 — 명령 구조까지 word 단위로 동일함을 Vivado 시뮬레이션으로 확인)이므로
   전환 방향(NORMAL→LOW_LIGHT vs 그 반대)에 무관하게 ICAP 시간이 같다 — Stage 5
   문서에서 이미 확인한 "partial bitstream 크기는 로직 사용량이 아니라 pblock 프레임
   수로 결정된다"는 사실의 직접적 귀결.
3. **30fps 프레임 예산(33.3 ms) 대비:** peak 가정 1.72 ms는 예산의 5.1%, 전형값
   6.87 ms는 20.6%. 스케줄러가 장면 단위로만 전환(빈번하지 않음)하므로, 이 수치가
   맞다면 재구성이 프레임 드롭 없이 "한 프레임 슬랙 안에" 들어갈 여지가 있어 보이나,
   **이는 어디까지나 이론적 계산이고 ④(드라이버/FSM 오버헤드)가 빠져 있어 실제
   재구성 지연을 과소평가할 가능성이 크다.** 확정 판정은 보드 실측 전까지 유보.
4. **드라이버 오버헤드가 왜 계산 불가능한가:** 이 프로젝트는 Stage 5까지 **fabric-only
   특성화**(PS/DDR 미통합, ICAP 컨트롤러/드라이버 미합성)만 완료했다. 실제 재구성은
   PS 측에서 partial bitstream을 SD카드/DDR에서 읽어 PCAP 또는 ICAP로 스트리밍하는
   드라이버 코드가 필요하며, 이 코드의 실행 시간은 **존재하지 않는 코드의 실행
   시간**이므로 계산할 방법이 없다 — 지어내지 않고 TODO로 유지.

## 4. 재현

```python
bitstream_bytes = 686664                    # rm_normal_partial.bit == rm_lowlight_partial.bit (오늘 재측정)
icap_peak_bw    = 100e6 * 4                 # AMD UG570 ICAPE3: 100 MHz x 32-bit
icap_typical_bw = 100e6                     # 보수적 전형값 (드라이버 오버헤드 포함 가정)
icap_time_peak    = bitstream_bytes / icap_peak_bw     # 1.7167 ms
icap_time_typical = bitstream_bytes / icap_typical_bw  # 6.8666 ms

drain_normal_ns    = 171 * 5.0   # rm_normal_tone_top min latency (cycles) x target period
drain_lowlight_ns  = 74  * 5.0   # rm_low_light_tone_top min latency (cycles) x target period
```
(drain/warm-up 사이클 출처: `/tmp/hls_dfxisp/dfxisp_accel/proj_rm_{normal,low_light}_tone_top/
solution1/syn/report/rm_*_tone_top_csynth.rpt`, 오늘 재합성.)

## 5. 남은 작업 (보드 전용, TODO)

- PR 컨트롤러 FSM(drain 신호 대기 → ICAP arm → partial bitstream 스트리밍 → new-RM
  ready 확인) 합성 및 실제 사이클 측정.
- ICAP 실효 대역폭 실측(스펙 peak 400MB/s와 실측치의 괴리 확인).
- PS↔PL 핸드셰이크(인터럽트/폴링) latency.
- 재구성 전후 **첫 프레임 왜곡/드롭 여부** 실측(파이프라인이 완전히 비워지지
  않은 상태에서 재구성이 시작되면 출력이 깨질 수 있음 — 이 문서의 ①drain 단계가
  실제로 방벽 역할을 하는지 보드에서 확인 필요).

## 산출물
이 문서 자체(계산 스크립트 §4 포함). 원자료: `results/stage4-hw-synthesis-2026-07-02.md`
(drain/warm-up 사이클), `results/stage5-dfx-implementation-2026-07-02.md`(bitstream 크기).
