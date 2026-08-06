<!--
=============================================================================
File   : isppipeline/hls/results/dfx-vivado-considerations-2026-07-03.md
Date   : 2026-07-03
Time   : 00:25 KST
Function: Vivado로 실제 DFX(Dynamic Function eXchange)를 구현해본 전 과정에서
          부딪힌 문제·트러블슈팅·실측치를 하나로 모은 "FPGA에서 DFX 적용 시
          고려사항" 종합 문서. 미비/불완전한 수치도 근거·한계와 함께 모두 기록.
Goal   : "vivado 시뮬레이션으로 DFX 전환 과정의 latency, 리소스 소모 등 고려사항을
         모두 수집해. 미비한 수치여도 모두 기록" 요청에 대한 응답.
=============================================================================
-->
# FPGA에서 DFX 적용 시 고려사항 종합 (Vivado 2024.1 실측/시도 기반, 2026-07-03)

> 이 문서는 이론이 아니라 **이 프로젝트에서 실제로 Vivado를 돌리며 부딪힌 것들**을
> 모은 것이다. 성공한 방법론, 실패해서 우회한 방법, 아직도 못 얻은 수치까지 전부
> "근거" 열에 신뢰도를 표시해 기록한다 — ✅실측 / 🧮계산(스펙 유도) / ⚠️시도했으나
> 실패 / ❌미착수(TODO).

---

## 1. 자원(Resource) 소모

### 1.1 RP(Reconfigurable Partition) 자체의 자원 오버헤드
| 항목 | 값 | 근거 |
|---|---|---|
| unified top(두 RM 상주, DFX 없음) | LUT 8,264/FF 5,536/BRAM 9/DSP 24 | ✅실측(csynth) |
| RM_NORMAL_TONE 독립 top(자체 AXI 포함) | LUT 5,202/FF 3,797/BRAM 4/DSP 12 | ✅실측(csynth) |
| RM_LOW_LIGHT_TONE 독립 top(자체 AXI 포함) | LUT 4,204/FF 3,243/BRAM 8/DSP 9 | ✅실측(csynth) |
| Config1 routed(static+RM_NORMAL 전체) | LUT 3,953/BRAM 1.5tile/DSP 12 | ✅실측(post-route) |
| Config2 routed(static+RM_LOW_LIGHT 전체) | LUT 2,922/BRAM 3.5tile/DSP 8 | ✅실측(post-route) |

**고려사항:** 독립 top(standalone RM)의 자원이 unified top 내부 sub-instance보다
크다(예: LUT normal 5,202 vs 3,108) — **RM을 독립 top으로 분리하면 자체
m_axi/s_axilite 인프라가 각각 붙기 때문**. RP 진입점을 설계할 때 이 오버헤드를
반드시 계산에 넣어야 한다(공유 인프라를 통째로 RM 안에 넣을지, static에 남길지
선택이 자원에 직접 영향).

### 1.2 pblock(RP 물리 영역) 용량과 실제 배치의 괴리 — 2026-07-03 재확인
| 항목 | 값 | 근거 |
|---|---|---|
| pblock 정의 | `CLOCKREGION_X0Y0:CLOCKREGION_X1Y0`(2개 클럭 리전) | ✅실측(Tcl 스크립트) |
| pblock 총 용량 | LUT 8,640 / BRAM 12 tile / DSP 96 | ✅실측(`report_utilization -pblocks`) |
| **클럭 리전별 실제 기여도** | **X0Y0: 0.06% / X1Y0: 99.94%** | ✅실측(Clock Region Statistics 표, `pblock_capacity.rpt`) |

**고려사항(중요, 이번에 새로 발견):** pblock을 2개 클럭 리전에 걸쳐 정의했다고
해서 용량이 균등하게 2배가 되는 게 아니다 — **실제로는 한쪽 리전이 거의 전부를
차지하고 나머지는 오차 수준으로 기여**했다. `resize_pblock`으로 범위를 넓힌 뒤에는
반드시 `report_utilization -pblocks`의 Clock Region Statistics 표로 **실제 배분을
검증**해야 한다 — 이름만 보고 "2배 여유"라고 가정하면 위험하다.

### 1.3 BRAM이 가장 먼저 조여드는 자원
LUT 여유율(RM_NORMAL 3,953/8,640=46% 사용, 낮음)에 비해 **BRAM Tile 여유
(Config2 3.5/12=29% 사용)은 상대적으로 더 빡빡**하다 — LUT 기준으로 "여유 있다"고
판단해도 BRAM 기준으로는 다를 수 있으니 **RP 후보를 늘릴 때는 자원 4종(LUT/FF/
BRAM/DSP)을 모두 개별적으로 pblock 용량과 대조**해야 한다.

---

## 2. Latency (재구성 지연)

### 2.1 단계별 분해
| 단계 | 값 | 근거 |
|---|---|---|
| Drain(진행 중인 파이프라인 flush) | 0.370~0.855 µs (74~171 cycle @ 5.0ns) | ✅실측(csynth latency) |
| ICAP 전송(partial bitstream 로드) | peak 1.716ms / 전형 6.865ms | 🧮계산(payload 686,532B 실측 ÷ AMD UG570 spec 대역폭) |
| Warm-up(새 RM 파이프라인 채움) | 0.370~0.855 µs | ✅실측(csynth latency) |
| ICAP 드라이버/FSM 오버헤드 | — | ❌미착수(PR 컨트롤러 자체가 설계에 없음, §4 참조) |

**고려사항:** ICAP 전송이 전체의 >99.9%를 차지해 drain/warm-up은 예산에서 무시
가능한 수준이다 — **재구성 latency를 줄이려면 partial bitstream 크기(=pblock
프레임 수)를 줄이는 것이 유일하게 효과적인 레버**다(로직 사용량을 줄여도
bitstream 크기는 그대로라는 §3.2와 연결).

### 2.2 Vivado 시뮬레이션으로 실측을 시도했으나 실패한 경험 (중요한 교훈)
실제 `rm_lowlight_partial.bit`를 ICAPE3 UNISIM 모델에 직접 흘리는 테스트벤치를
만들어 5회 반복 실행(`pr-latency-vivado-sim-2026-07-02.md`):
- ✅ SYNC는 word index 2에서 정상 성공(bitstream 구조 유효성 확인).
- ⚠️ `PRDONE` 신호, raw DESYNC 패턴 매칭, 내부 `desync_flag` 신호 관측 — **3가지
  독립 방법 모두 격리된 테스트벤치에서 완료 신호를 못 얻음.**
- **원인(2026-07-03 재확인으로 뒷받침됨):** `PRDONE`은 `eos_startup`에 의존하는데,
  이는 `STARTUPE3` 프리미티브가 관장하는 신호다. 그런데 **이 설계에는 애초에
  `STARTUPE3`/`ICAPE3`가 인스턴스화되어 있지 않다**(§4.1) — 격리 시뮬레이션이
  실패한 게 아니라, **"진짜로 완료 신호를 만들어낼 로직 자체가 아직 없다"**는
  것이 근본 원인이었다.

**고려사항:** DFX 프로젝트에서 partial reconfiguration의 "완료"를 시뮬레이션으로
검증하려면 **RP/static 넷리스트만으로는 부족하고, `STARTUPE3`(및 이를 구동하는
PR 컨트롤러 FSM)가 설계에 포함된 시점부터 의미가 있다.** ICAPE3를 단독으로
테스트벤치에 물려서 "시뮬레이션으로 재구성 시간을 재겠다"는 접근은 이 UNISIM
모델의 설계 의도와 맞지 않을 가능성이 높다 — 처음부터 계획에 "PR 컨트롤러 합성 →
전체 디바이스급 시뮬레이션 또는 실보드"를 넣는 게 낫다.

### 2.3 프레임 예산 대비
30fps 기준 프레임 예산 33.3ms 대비, peak ICAP 추정(1.72ms)은 5.1%, 전형값(6.87ms)은
20.6% — **드라이버 오버헤드가 빠진 상태**이므로 실제로는 이보다 클 가능성이 크다.
🧮계산치이며 ❌실측 아님.

---

## 3. Bitstream 크기와 그 결정 요인

| 항목 | 값 | 근거 |
|---|---|---|
| Full bitstream | 19,311,211 bytes ≈ 19.3 MB | ✅실측 |
| Partial bitstream(RM_NORMAL_TONE) | 686,664 bytes(헤더 포함) / 686,532 bytes(payload) | ✅실측 |
| Partial bitstream(RM_LOW_LIGHT_TONE) | **동일**(686,664 / 686,532 bytes) | ✅실측 |
| DESYNC 명령 위치(word index, 두 RM 모두) | 6414, 6724, 165183, **171615**(진짜) | ✅실측(파일 직접 파싱) |

**고려사항(반증된 가정):** "로직을 더 많이 쓰는 RM이 partial bitstream도 더 크다"는
**직관은 틀렸다.** partial bitstream 크기는 **pblock의 reconfigurable frame
개수**(고정된 물리적 그리드)로 결정되며, 그 안에서 실제로 몇 개의 LUT/BRAM/DSP를
쓰는지와는 무관하다 — LUT 3,953을 쓰는 RM과 LUT 2,922를 쓰는 RM의 partial
bitstream이 byte 단위로 동일한 이유. **DFX 자원 절감 효과를 논할 때 "bitstream이
작아진다"고 기대하면 안 되고, 오직 static 영역 밖으로 로직이 빠져나가는 것 자체
(칩 면적/전력)에서만 이득을 봐야 한다.**

---

## 4. PR(Partial Reconfiguration) 컨트롤러 — 이번에 발견한 가장 큰 설계 공백

| 항목 | 값 | 근거 |
|---|---|---|
| `ICAPE3` 사용 개수(config1) | **0** / 가용 2 | ✅실측(2026-07-03, `report_utilization` §8) |
| `ICAPE3` 사용 개수(config2) | **0** / 가용 2 | ✅실측(2026-07-03) |
| `STARTUPE3` 사용 개수(양쪽) | **0** / 가용 1 | ✅실측(2026-07-03) |

**고려사항:** 지금까지의 모든 "DFX 구현"은 **static+RM이 물리적으로 정합함
(pr_verify PASS)을 증명한 것**이지, **누가 언제 재구성을 트리거하고 실제로 ICAP에
데이터를 흘려보낼지는 전혀 설계되지 않았다.** 실제 DFX 시스템을 board에 올리기
전에 최소한 다음이 필요하다:
1. `STARTUPE3`(또는 PS의 PCAP)를 통한 재구성 트리거 경로.
2. Drain 감지(파이프라인이 비었는지 확인하는 backpressure/busy 신호) — 현재 RM
   top들은 `ap_done`/`ap_idle` 류의 표준 HLS 제어 신호는 있지만, **이를 PR
   컨트롤러가 "drain 완료"로 해석하는 로직이 없다.**
3. partial bitstream을 저장할 곳(SD카드/DDR/QSPI)과 그로부터 ICAP까지의 DMA/스트리밍
   경로 — PS/DDR이 아예 통합되지 않은 현재 fabric-only 설계에는 존재 자체가 불가능.

---

## 5. 이번 세션 중 실제로 재현한 Vivado DFX 방법론 문제와 해법 (트러블슈팅 로그)

과거 세션들에서 실제로 겪고 해결한 문제들 — 다음에 유사 프로젝트를 할 때 반드시
재확인해야 할 체크리스트:

| # | 문제 | 증상 | 해법 |
|---|---|---|---|
| 1 | **I/O 핀 초과** | RM 포트(~110개, 662 signal bit) 전부를 칩 top-level 핀으로 노출 → `Number of unplaced IO Ports (662) > available pins (360)` | fabric-only 특성화 목적에 맞춰 `ap_clk`/`ap_rst_n`만 실제 칩 I/O로 남기고 나머지는 내부 tie-off(입력=0, 출력=관찰용 XOR reduce) |
| 2 | **RP 인스턴스가 죽은 로직으로 제거됨** | `dont_touch`/`keep_hierarchy` 없이 `synth_design`이 RP를 최적화로 삭제 → `No cells matched 'u_rp'` | 속성 추가로 RP 인스턴스 보존 |
| 3 | **SNAPPING_MODE가 pblock을 0으로 축소** | 단일 클럭 리전(X0Y0)에 `SNAPPING_MODE ON`을 걸면 유효 영역이 0으로 축소(해당 리전의 reconfigurable frame 경계 정렬 이슈로 추정) | 2리전·스냅핑 OFF로 전환(단, §1.2처럼 실제로는 1개 리전 분량만 확보되는 부작용 있음) |
| 4 | **Config2를 독립적으로 place하면 static 배치가 미세하게 어긋남** | Config1과 Config2의 static 영역이 site 단위로 불일치 → `pr_verify` 실패(instance 배치 불일치) | Config1의 **완전히 구현된** 체크포인트에서 `update_design -cell u_rp -black_box` + `lock_design -level routing`으로 static을 고정한 뒤 RM2를 이식(AMD UG909 표준 절차) |
| 5 | **`write_bitstream`이 DRC로 막힘** | `ap_clk`/`ap_rst_n`에 실제 보드 핀이 없어 NSTD-1/UCIO-1 DRC 위반 | `set_property SEVERITY {Warning} [get_drc_checks NSTD-1/UCIO-1]`로 낮춰 우회(가짜 핀을 지어내지 않고 문서화) |
| 6 | **DFX 후보 RM 간 포트 목록 불일치** | 같은 RP 슬롯에 들어갈 두 RM의 포트 순서/타입/개수가 다르면 DFX 자체가 성립 안 함 | `rm_normal_tone_top`/`rm_low_light_tone_top`을 처음부터 동일 포트 시그니처로 설계(§HLS README) |
| 7 | **격리 ICAPE3 시뮬레이션으로 완료 신호를 못 얻음** | §2.2 참조 | 해결 안 됨 — PR 컨트롤러(§4) 합성 후 재시도 필요 |

---

## 6. Timing closure — 실제 제약 하 WNS (2026-07-03, 이번 세션 신규 실측)

Stage 5까지는 "타이밍 제약을 걸지 않은 fabric-only 특성화라 WNS 미측정"으로
남겨뒀던 항목이다. 이번에 `create_clock -period 5.000 [get_ports ap_clk]`을 걸고
config1/config2를 **처음부터 opt/place/route를 다시 실행**(정확한 방법론 유지 —
config2는 config1의 새 타이밍 결과 체크포인트를 black-box+lock해서 재구현)해
실제 WNS를 확보했다.

| 지표 | Config1(static+RM_NORMAL) | Config2(static+RM_LOW_LIGHT) | 근거 |
|---|---|---|---|
| Target clock | 200 MHz (5.000 ns) | 200 MHz (5.000 ns) | ✅실측(제약 반영) |
| **WNS** | **+0.619 ns**(제약 만족) | **+1.930 ns**(제약 만족) | ✅실측 |
| 환산 critical path | 4.381 ns | 3.070 ns | 🧮계산(period−WNS) |
| 환산 max Fmax | **≈228.3 MHz** | **≈325.7 MHz** | 🧮계산(1/critical path) |
| TNS / 실패 endpoint | 0.000 / 0 | 0.000 / 0 | ✅실측 |
| pr_verify(재구현 후) | **PASS**(static tile 29,648·cell 256 동일) | 동일 | ✅실측 |
| 자원(재구현 후) | LUT 3,957(§4.2 원래 3,953과 거의 동일) | LUT 2,894(원래 2,922와 거의 동일) | ✅실측 |

**고려사항:**
1. **두 config 모두 200MHz 목표를 만족**한다(양쪽 다 양의 slack) — fabric-only
   특성화라도 실제 클럭 제약을 걸면 유의미한 timing closure 정보를 얻을 수 있다는
   뜻. "PS/DDR 통합 전이라 타이밍을 잴 수 없다"는 가정은 **부분적으로만 사실**
   이었다(clock/reset "핀"은 없어도 clock "제약"은 걸 수 있다).
2. **Config2(RM_LOW_LIGHT_TONE)가 Config1(RM_NORMAL_TONE)보다 여유가 3배 이상
   크다**(325.7MHz vs 228.3MHz 환산) — static 영역은 완전히 동일(black-box+lock로
   고정)하므로 이 차이는 순수하게 RM 내부 로직의 critical path 차이다. csynth
   추정(273.97MHz, 두 RM 동일하게 보고)과 실제 post-route(228~326MHz, RM마다
   다름)가 갈린다 — **csynth의 achievable clock 추정은 RM 간 실제 post-route
   타이밍 차이를 반영하지 못한다**는 것이 이번에 드러난 새 사실.
3. 자원 수치는 재구현 전후로 거의 변하지 않음(LUT ±4개 수준) — 타이밍 제약을
   걸어도 이 정도 규모/여유율의 설계에서는 배치가 크게 안 흔들린다는 뜻.

## 7. 종합 체크리스트 (다음 DFX 프로젝트를 위한 요약)

- [ ] RM 후보의 자원을 **standalone top으로 별도 합성**해 실제 DFX 풋프린트를 먼저
      확인하라(unified top 내부 sub-instance 수치만 보면 과소평가한다, §1.1).
- [ ] pblock을 여러 클럭 리전에 걸쳐 정의했다면 `report_utilization -pblocks`의
      Clock Region Statistics로 **실제 배분을 반드시 검증**하라(§1.2).
- [ ] 4종 자원(LUT/FF/BRAM/DSP) 각각에 대해 pblock 용량 대비 여유율을 따로
      확인하라 — 한 자원만 보고 "여유 있다"고 판단하지 말 것(§1.3).
- [ ] partial bitstream 크기는 **로직 사용량이 아니라 프레임 그리드**로 결정된다는
      점을 초기 견적 단계에서부터 반영하라(§3).
- [ ] **PR 컨트롤러(ICAPE3/STARTUPE3 + drain 감지 + bitstream 스트리밍 경로)를
      RP/static 설계와 동시에 계획하라** — 나중에 추가하면 재구성 시뮬레이션 자체가
      불가능하다는 것을 이번에 확인했다(§2.2, §4).
- [ ] DFX 방법론 트러블슈팅(§5)은 AMD UG909을 읽는 것만으로는 부족하고, 실제로
      한 번 끝까지 돌려봐야 드러나는 문제들이었다 — 특히 #3(SNAPPING_MODE)과
      #4(black-box+lock)는 문서에 명시적으로 안 나와 있어 직접 겪어야 알았다.

## 산출물
- 2026-07-03 timing-constrained 재구현 산출물(`/tmp` 산출물, git 비추적,
  재현 절차는 §6 서두 참조): `config{1,2}_normal_timed.dcp`, `config2_lowlight_timed.dcp`,
  `config{1,2}_timed.timing.rpt`, `config{1,2}_timed.util.rpt`, `pr_verify_timed.rpt`.
- 근거: `pblock_capacity.rpt`, `config{1,2}_impl.util.rpt`, `pr-latency-breakdown-2026-07-02.md`,
  `pr-latency-vivado-sim-2026-07-02.md`, `stage5-dfx-implementation-2026-07-02.md`.
