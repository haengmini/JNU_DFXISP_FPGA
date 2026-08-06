<!--
=============================================================================
File   : isppipeline/hls/results/design-limitations-2026-07-03.md
Date   : 2026-07-03
Time   : 00:20 KST
Function: 현재 DFXISP 설계(reset 아키텍처, ver1, Stage 1~5 완료 시점)의 한계점을
          알고리즘/SW eval/HW synthesis/DFX 구현/시뮬레이션 5개 층위로 나눠 종합.
Goal   : "지금 설계의 한계점을 분석하고 보고서 작성해" 요청에 대한 응답. 이미
          개별 문서에 흩어져 있던 한계·가정·TODO를 하나로 모아 우선순위와 함께
          정리 — 새 발견보다는 지금까지 쌓인 근거의 종합/구조화가 목적.
=============================================================================
-->
# DFXISP 설계 한계점 종합 보고서 (2026-07-03)

> 이 문서는 새로운 실험이 아니라 **지금까지 Stage 1~5에서 실측·발견한 한계를 5개
> 층위(알고리즘/SW eval/HW synthesis/DFX 구현/시뮬레이션)로 구조화**한 종합
> 보고서다. 각 항목에 원본 근거 문서를 링크한다.

## 요약 (한 줄씩)

| # | 층위 | 한계 | 심각도 |
|---|---|---|---|
| 1 | 알고리즘 | tone RM이 mAP를 개선하지 못함(guardrail 탈락) | **높음** — 방향 A로 이미 우회 |
| 2 | 알고리즘 | low-light RM 손상의 원인이 baseline core의 정적 WB(저조도)/해상도 손실(정상조도)로 조건별 상이 | 높음 |
| 3 | SW eval | pseudo-RAW proxy — 실제 센서 노이즈·real-RAW 없음 | 높음(최종 판정 불가) |
| 4 | SW eval | 표본 크기 작음(n=31~150), 단일/소수 detector | 중간 |
| 5 | HW synthesis | csynth 자원/타이밍은 추정치(post-route WNS는 2026-07-03에 구 pblock 기준으로만 확보 — §3.1) | 낮음(구 pblock만 해소, 신 pblock 재검증 TODO) |
| 6 | DFX 구현 | fabric-only 특성화 — PS/DDR 미통합, 실제 클럭/리셋 핀 없음 | 높음(보드 전 마지막 gap) |
| 7 | DFX 구현 | pblock 용량이 사실상 클럭 리전 1개분(2개 리전 중 1개가 0.06%만 기여) | 중간 |
| 8 | DFX 구현 | RP 경계가 "전체 모드 파이프라인" 단위 — baseline core가 RM마다 물리적으로 중복 | 중간 (설계 선택, 근본적 한계는 아님) |
| 9 | DFX 구현 | ICAPE3/STARTUPE3가 설계에 전혀 없음 — 실제 PR 컨트롤러 미존재 | **높음** — Stage 6 선결 과제 |
| 10 | 시뮬레이션 | Vivado XSIM으로 ICAP 완료 신호를 격리 환경에서 얻지 못함 | 중간(우회 가능, 대안 있음) |
| 11 | 검증 방법론 | golden model이 실제 버그를 미러링해 bit-exact 테스트가 못 잡은 전례 | 중간(재발 방지책 필요) |
| 12 | 알고리즘(Stage 2) | tone RM 파라미터 스윕이 계획만 되고 실행되지 않음(단일값만 측정) | 낮음(방법론 부채) |
| 13 | 알고리즘(Stage 2) | COCO 오적용 시 과포화(11.25%)가 이후 3차례 파라미터 변경 후 재검증 안 됨 | 중간(회귀 미확인) |
| 14 | 알고리즘(Stage 2) | Policy A 채택 근거(DPU ABI)가 Stage 6 전에는 검증 불가능한 잠정 결정 | 낮음(Stage 6 의존) |
| 15 | HW synthesis(Stage 4) | L3(RTL wrapper sim, AXI-Stream)가 L2 co-sim 실패로 인해 아예 착수되지 못함 | 중간(검증 체인 단절) |
| 16 | HW synthesis(Stage 4) | Arm1(정적 baseline-only, static all-resident) 자원/타이밍이 한 번도 측정된 적 없음 | 중간(3-arm 비교 미완결) |
| 17 | 알고리즘(checker) | `dark_ratio<0.20`(복귀 임계값)과 leaf 상수 `DARK_Y=50`이 프로젝트 시작(6/4)부터 지금까지 한 번도 재검증되지 않음 | 중간(진입 쪽만 재보정됨) |
| 18 | 알고리즘(checker) | `scheduler_sim.py`로 검증한 히스테리시스 정책(narrow band+N=3)이 synthetic luma 기준이라 실제 `dark_ratio` 파이프라인과 미연결 | 낮음(정책 형태는 유효, 배선만 안 됨) |
| 19 | DFX 구현 | pblock 재floorplan 전후 partition pin 15→3 감소 원인 미조사 | 낮음(pr_verify는 PASS라 기능적 영향 없음) |

---

## 1. 알고리즘 층위

### 1.1 mAP guardrail 탈락 (근본 한계)
YOLOv8n/s + SSDLite-MobileNetV3, ExDark+COCO 전 조건에서 **`none`(무처리)이 항상
최고**, `normal`/`lowlight` tone RM은 오히려 mAP를 낮춘다(`experiment-report-2026-07-02.md`
§4.3). ver1 개정(gain/gamma 추가)으로 격차를 좁혔지만 역전하지 못함
(`experiment_ver1_2026-07-02.md`). **이 프로젝트는 이 한계를 "방향 A"(mAP 근거가
아니라 자원/전력 근거로 DFX를 정당화)로 우회했지, 해결한 것이 아니다** — 최종
판정은 여전히 보드 DPU + real-RAW 재학습 검출기에서만 가능(SW proxy 천장 가설).

### 1.2 low-light RM 손상 원인이 조도 조건마다 다름
ablation 실측(`lowlight-rm-map-rootcause-2026-07-02.md`) 결과 **단일 원인이 아니다**:
- ExDark(저조도, RM의 목표 조건): 손실의 약 70%가 **두 RM이 공유하는 baseline
  core의 BLC/WB**에서 발생, 해상도 손실은 −1.4%뿐.
- COCO(정상조도, 오적용 시): 반대로 해상도 손실이 지배적(−11.7%), BLC/WB는 −1.9%.

**한계:** 이 발견은 "왜 안 되는지"는 밝혔지만 "어떻게 고칠지"는 아직 실험하지
않았다. 저조도 조건에서 WB를 완화하는 scene-adaptive 설계, Policy B(해상도 보존)
전환 등은 §5의 "다음 실험 후보"로만 남아있다.

### 1.3 checker 임계값의 대상 조건 한정성
`DARK_RATIO=0.80`(ver2 재보정, `experiment_ver2_2026-07-02.md`)은 `data/{coco_val,
exdark_val}` 두 데이터셋의 **분포 스윕으로 얻은 값**이라, 다른 조도 분포(예: 황혼,
실내조명 혼합)에서 일반화된다는 보장이 없다.

### 1.4 Stage 2(tone RM 파라미터 확정) — 계획과 실행의 간극 (2026-07-03 추가)

Stage 2 결과(`stage1-3-results-2026-07-02.md`, `results/image_metrics_{exdark,coco}.csv`)를
다시 감사한 결과 3가지 미완결 항목을 발견:

- **파라미터 스윕 미실행:** `experiment-stages-2026-07-02.md`가 계획한 스윕 후보
  (binning kernel, gain{1.25,1.5}, gamma{3.0,4.0})는 CSV상 arm당 **단일 조합만**
  측정됐다(lowlight = gain1.25/gamma4.0 딱 하나). "스윕해서 확정"이 아니라 "첫 값을
  채택하고 다음 단계로 넘어간" 것 — 이후 Stage 3에서 gain/gamma가 여러 번 바뀐 것은
  mAP 기준 재조정이었지 Stage 2가 계획한 이미지 지표 기준 스윕은 아니었다.
- **COCO 오적용 과포화 회귀 미확인:** Stage 2 실측(gain1.25/gamma4.0 기준) 당시
  COCO에 lowlight가 잘못 적용되면 포화율 11.25%까지 치솟는 문제를 발견했으나,
  이후 gain(1.25→2.0, ver1)·BLC(256→128, 라운드 3b) 등 관련 파라미터가 3차례
  바뀌는 동안 포화율을 재측정한 기록이 전혀 없다(`ver1`/`ver2`/`blc-fix-resynthesis`
  문서 어디에도 포화율 언급 없음). checker 재보정(ver2)으로 이 경로를 탈 빈도는
  줄었지만(과트리거 79.5%→11%), **발생했을 때의 심각도는 최신 파라미터 기준으로
  미확인 상태**다.
- **Policy A/B 결정이 미검증 가정에 의존:** Policy A(해상도 반감 유지) 채택 근거는
  "DPU ABI가 고정 해상도를 요구할 때만 Policy B 검토"였는데, DPU 통합은 Stage 6에서만
  가능해 **이 결정 자체가 실제 요구사항과 한 번도 대조되지 않았다.** 참고로 라운드
  3a-i ablation의 `ll_fullres_tone`(Policy B에 가까움)이 손상을 크게 줄이는 정황
  (COCO −3.5% vs Policy A의 −15.8%)이 나왔지만 `lowlight-rm-map-rootcause-2026-07-02.md`
  §5(b)에 "후속 액션 후보"로만 남아있고 실행되지 않았다.

### 1.5 checker 임계값 체계 — 진입 쪽만 재보정되고 나머지는 최초 설계부터 미검증 (2026-07-03 추가)

git 히스토리로 checker의 전체 계보를 추적한 결과, 지금의 `dark_ratio` 방식은 reset(7/1)
때 새로 만든 게 아니라 **프로젝트 최초 설계(2026-06-04, `docs/Research_Roadmap.md`,
커밋 `4b0c570`)부터 있던 것**이다:

```text
2026-06-04(원본)  Y=(R+2G+B)/4, dark_ratio=mean(Y<50)
                  NORMAL→LOWLIGHT: dark_ratio>0.4, LOWLIGHT→NORMAL: dark_ratio<0.2
2026-06-28        scene_average(raw) < low_light_threshold 로 단순화(런타임 파라미터,
                  하드코딩 기본값 없음) — 원본과 다른 계열의 임시 구현
2026-07-01 reset  dark_ratio 방식으로 원본 설계 복원(0.4/0.2 그대로)
2026-07-02 ver2   진입 쪽만 Youden's J로 0.4→0.8 재보정(`experiment_ver2_2026-07-02.md`)
```

**두 가지가 6/4 이후 한 번도 재검증되지 않은 채 남아있다:**

- **`DARK_Y=50`(leaf 상수):** `dark_ratio`가 세는 "어두운 픽셀"의 기준 자체. 3가지
  경로(HW C++, golden Python, SW eval) 어디에도 이 값의 도출 과정이 없고, 모든 문서가
  스스로 "naive"라고 부른다. ver2의 Youden's J 스윕조차 **이 값을 고정한 채** `dark_ratio`
  임계값만 스윕했으므로, `Y<50` 자체가 최적이 아니라면 0.8이라는 결론도 재검토 대상이다.
- **`dark_ratio<0.20`(복귀 임계값):** 진입 쪽(0.4→0.8)만 실측 재보정됐고, 복귀 쪽은
  6/4 원본에서 대칭적으로 짐작해 넣은 0.2가 지금까지 그대로다. `dfxisp_accel`의 실제
  HW AUTO checker는 애초에 진입 쪽만 구현하고("no exit/hysteresis", `RESEARCH.md` §5.1
  주석) 복귀 쪽은 스케줄러 레이어의 몫으로 미뤄뒀는데, 그 스케줄러 레이어(§1.6 참조)도
  이 값을 실제로 검증한 적이 없다.

### 1.6 히스테리시스 정책 검증이 실제 checker 파이프라인과 미연결 (2026-07-03 추가)

`results/scheduler_sweep.csv`의 27케이스 스윕(narrow band+temporal_N=3이 최적)은
**synthetic 12-bit luma 시퀀스**(`scheduler_sim.py`의 `build_sequence()`가
`np.random.default_rng(seed=7)`로 생성)를 대상으로 한다 — 실제 `dark_ratio`/`Y<50`
파이프라인과 코드상 연결이 전혀 없다. 원래 계획(최초 설계 문서의 `Dataset/DynamicSwitch/
sequence.json`, ExDark/COCO 실제 이미지를 밝음→어두움→밝음 순서로 배열한 진짜 시간축
시퀀스)은 파일도 코드도 존재하지 않는다 — 계획만 있고 구현되지 않은 채, 정책 동역학
검증용 임시 synthetic 데이터가 그 자리를 대신 메운 상태다. **narrow band+N=3이라는
결론(정책의 "형태")은 유효하지만, 실제 시스템의 `dark_ratio` 값 범위에 맞춰 재검증된
적은 없다.**

---

## 2. SW 평가(proxy) 층위

### 2.1 pseudo-RAW는 진짜 RAW가 아니다
데이터셋은 **이미 ISP된 JPEG을 역변환**한 것(RGGB nearest, shift8)이라 실제 센서의
포화 특성·색 잡음·CFA crosstalk을 반영하지 않는다. §1.2의 "저조도 WB가 색잡음을
증폭시킨다"는 가설은 **정량 검증되지 않았다**(실제 센서 노이즈 없이는 검증 불가) —
`lowlight-rm-map-rootcause-2026-07-02.md` §4.4/§7의 명시적 한계.

### 2.2 표본 크기와 검출기 종류
Stage 3 mAP는 n=71~150(dataset), ablation은 n=31~39로 더 작다 — 통계적 신뢰구간을
계산하지 않았고(단일 point estimate), 절대값이 아니라 **arm 순서**만 판단 근거로
쓴다는 전제가 프로젝트 전체에 깔려 있다. 검출기는 YOLOv8n/s(COCO 사전학습) +
SSDLite-MobileNetV3(torchvision, 다른 사전학습) 3종만 — 실제 목표인 Vitis-AI
`tf_ssdmobilenetv1`(양자화된 DPU 대상 모델)은 가중치 부재로 이 환경에서 실행
불가(`experiment-report-2026-07-02.md` §4.3).

---

## 3. HW C-synthesis 층위

### 3.1 자원/타이밍은 추정치 — WNS gap은 구 pblock 기준으로만 해소 (2026-07-03 수정)

Stage 4 수치(LUT/FF/BRAM/DSP, Fmax 273.97MHz)는 **Vitis HLS csynth 추정**이지
post-route 확정치가 아니다(단, Config1/Config2의 routed 자원 수치는 실측 확정치 —
`stage5-dfx-implementation-2026-07-02.md`). csynth의 273.97MHz는 **제약을 걸지 않은
achievable clock**이지, target(200MHz/5.0ns) 제약 하에서의 실제 WNS가 아니었다 —
timing-constrained 재구현을 실행해 실측 WNS를 확보했다(`dfx-vivado-considerations-2026-07-03.md`
§6): Config1(+0.619ns)·Config2(+1.930ns) 모두 200MHz 제약을 만족하며, **두 RM의
post-route 여유가 서로 다르다**(환산 max Fmax 228.3MHz vs 325.7MHz)는 사실은 csynth
추정만으로는 알 수 없었던 새 정보다.

> **주의(같은 날 안에 stale해진 항목):** 이 WNS 실측은 **구 pblock(X0Y0:X1Y0)** 기준이다.
> 이 문서 작성 이후 같은 날(`phase0-2-execution-2026-07-03.md` §2.2) pblock을
> `X1Y0:X2Y0`(용량 2배)으로 재floorplan했는데, **신 pblock에서는 timing-constrained
> 재구현을 다시 실행한 적이 없다** — 즉 지금 실제로 배포에 쓰일 최종 floorplan의 WNS는
> 여전히 미검증 상태다. 이 재검증은 PS/DDR 통합이 필요 없는 순수 fabric-only Vivado
> 작업이라 보드 없이 바로 실행 가능하다(§6 우선순위 제언 참조).

### 3.2 latency/interval 수치의 대표성
csynth latency(run_normal min 171cyc / run_low_light min 74cyc)는 **파이프라인
depth**이지 실제 프레임 처리 시간이 아니다. §4.4의 프레임 예산 환산(1280×720
기준 ≈3.4ms/0.84ms)은 "II=1이라 cycles≈픽셀수"라는 가정에 기반한 **추정**이며
별도 실행으로 직접 측정한 값이 아니다(`stage4-hw-synthesis-2026-07-02.md` §4.4).

### 3.3 검증 체인 단절 — L3(RTL wrapper sim)이 착수조차 되지 못함 (2026-07-03 추가)

`SPEC.md` §8의 bit-exact 전파 체인(L0~L5)에서 L2(C/RTL Co-sim)가 `stage4-hw-synthesis-2026-07-02.md`
§6b에 기록된 SIGSEGV로 자동 비교를 완주하지 못한 채 멈췄고, 그 다음 단계인 **L3(AXI-Stream으로
감싼 실제 wrapper 시뮬레이션)는 아예 시도된 적이 없다.** `experiment-stages-2026-07-02.md`가
Stage 4의 검증 기준으로 못박은 "L1==L2==L3 bit-exact"가 L2에서 끊긴 채 L3로 넘어가지
못했다는 뜻 — L2가 재개되지 않는 한 L3도 계속 공백으로 남는다.

### 3.4 Arm1(정적 baseline-only) 자원/타이밍 미측정 — 3-arm 비교의 한 축이 비어있음 (2026-07-03 추가)

`experiment-plan-2026-07-01.md`은 "핵심 비교축: Arm2 vs Arm3, **Arm1 vs Arm2**"라고
명시했지만, `SPEC.md` §10 Arm 비교표는 Arm2(register-only)·Arm3(DFX)만 실측치이고
**Arm1(baseline core + RM_NORMAL_TONE identity만 정적 상주) 칸은 전부 `TODO`다** —
LUT/FF/BRAM/DSP, Fmax, 전력 어느 것도 별도 top으로 분리 합성된 적이 없다. Arm2/Arm3는
Stage 4~5에 걸쳐 여러 라운드 재측정됐지만 Arm1은 한 번도 실측 대상이 된 적이 없어,
"register-only 대비 DFX 순이득"(Arm2 vs Arm3)은 확보됐어도 "DFX/register-only가
가장 단순한 정적 baseline 대비 실제로 얼마나 나은지"(Arm1 vs Arm2/3)는 여전히 공백이다.

---

## 4. DFX(Vivado) 구현 층위

### 4.1 fabric-only 특성화 — 가장 큰 남은 gap
PS/DDR/AXI interconnect 미통합, 실제 clock/reset 핀 배정 없음(NSTD-1/UCIO-1 DRC를
Warning으로 낮춰 우회), 절대 전력·PR latency 실측·DPU end-to-end 전부 TODO(보드).
**이 프로젝트가 "보드 측정 전단계"라 자평하는 지점이 바로 여기** — Stage 6 하나로
남음.

### 4.2 pblock 용량이 사실상 클럭 리전 1개분
`pblock_capacity.rpt`(2026-07-03 재확인): 2개 클럭 리전(X0Y0:X1Y0)으로 floorplan
했지만 **실제로는 X1Y0이 용량의 99.94%, X0Y0은 0.06%만 기여**한다(Clock Region
Statistics 표). 즉 "2개 리전"이라는 이름과 달리 사실상 1개 리전 분량의 자원만
확보한 셈 — 여유(LUT 8,640/BRAM 12 tile/DSP 96)가 넉넉해 문제가 되지 않았을 뿐,
더 큰 RM을 설계했다면 이 착시가 용량 부족으로 이어질 수 있었다.

### 4.3 RP 경계 선택 — baseline core가 RM마다 물리적으로 중복
SPEC.md의 개념도는 "baseline core(BLC+WB+CCM)는 static, tone RM만 reconfigurable"
처럼 보이지만, **실제 합성된 Stage 5의 RP(`rm_normal_tone_top`/`rm_low_light_tone_top`)
는 데모자이크부터 tone까지 모드별 전체 파이프라인을 통째로 감싼다** — `apply_blc_wb12`
가 소스코드 레벨에서는 공유되지만, HLS가 이 RP 경계를 기준으로 각각 독립 합성하므로
실제 실리콘에는 BLC/WB 로직이 **RM마다 중복 구현**된다(`dfxisp-microarchitecture-2026-07-02.md`
설계 시 발견). 이것 자체가 버그는 아니지만, "baseline core 공유로 자원을 아낀다"는
직관적 기대와 실제 구현 사이의 간극이며, 더 세밀한 RP 분할(baseline core를 진짜
static 모듈로 분리)은 시도되지 않았다.

### 4.4 ICAP/PR 컨트롤러가 설계에 아예 없음
2026-07-03 재확인: config1/config2 어느 쪽 utilization report에도 **`ICAPE3`·
`STARTUPE3` 사용량이 0**이다(`config{1,2}_impl.util.rpt` §8 CONFIGURATION). 즉
지금까지의 "DFX 구현"은 **RP 스왑이 정적으로(pr_verify) 정합함을 증명했을 뿐,
실제로 무엇이 그 스왑을 트리거하고 수행할지(PR 컨트롤러)는 아직 설계되지 않았다.**
Stage 6 이전에 반드시 채워야 할 설계 공백.

### 4.5 partition pin 15→3 감소 원인 미조사 (2026-07-03 추가)

구 pblock(X0Y0:X1Y0) 기준 `pr_verify`는 partition pin 15개를 보고했는데, BLC fix+pblock
재floorplan을 결합한 최종 재구현(`blc-fix-resynthesis-2026-07-03.md` §4)에서는 3개로
줄었다(`SPEC.md` §10에 "원인 미조사"로 기록). **두 재구현 모두 pr_verify는 PASS**이므로
DFX 정합성 자체에는 영향이 없지만, 왜 파티션 경계를 통과하는 신호 수가 5분의 1로
줄었는지는 설명되지 않았다 — pblock 크기 변경이 어떻게 partition pin 수에 영향을
주는지(혹은 무관한 다른 요인인지) 확인되지 않은 채 남아있다. `report_partitions` 또는
두 `.dcp`(`deliverables/checkpoints/`에 이미 존재)의 직접 비교로 보드 없이 지금 바로
조사 가능하다.

---

## 5. 시뮬레이션 방법론 층위

### 5.1 격리된 ICAPE3 시뮬레이션의 한계
2026-07-02 시도(`pr-latency-vivado-sim-2026-07-02.md`): 실제 partial bitstream을
ICAPE3 UNISIM 모델에 흘렸을 때 SYNC는 성공했지만 PRDONE 및 내부 desync_flag
완료 신호를 3가지 독립 방법으로도 얻지 못함 — `eos_startup`이 `STARTUPE3` 기반의
전체 디바이스 시뮬레이션 컨텍스트를 요구하는 것으로 추정. **§4.4와 직접 연결**:
설계에 STARTUPE3가 아예 없으니 이 신호가 격리 환경에서 나올 수 없는 것이 당연했다
— 사후적으로 두 발견이 서로를 설명한다.

### 5.2 cosim(RTL/C 자동 bit-exact 비교) 미완주
Stage 4 §6b: RTL 시뮬레이션 자체는 7/7 성공했지만 자동 post-check 비교 단계가
WSL2+Vitis HLS 2024.1+XSIM 환경 특유의 하네스 문제로 SIGSEGV — "실행 성공 확인,
자동 비교 미완주"로 기록됐고 재시도되지 않았다.

### 5.3 golden model이 실제 버그를 미러링한 전례 (검증 방법론 자체의 맹점)
adversarial review로 발견된 색상손실 버그(`SPEC.md` §11.5)는 **C++ 구현과 Python
golden model이 같은 잘못된 알고리즘을 구현**했기 때문에 `make verify`(bit-exact
교차검증)로는 원리적으로 잡을 수 없었다 — 두 독립 구현이 아니라 사실상 "같은 저자가
같은 실수를 두 번 한" 상황. **재발 방지책이 아직 없다**: 예를 들어 SW eval
파이프라인(`isp_pipeline_ver1.py`)처럼 제3의 독립 구현과 상시 교차검증하는 CI
게이트는 없고, 이번에도 외부 adversarial review가 우연히 잡아낸 것.

---

## 6. 우선순위 제언 (2026-07-03 갱신 — 항목 1/2/4는 이후 Phase 12/13에서 완료됨)

> 이 절은 문서 최초 작성(00:20 KST) 시점 기준이었다. 같은 날 이후 진행된
> `phase0-2-execution-2026-07-03.md`(Phase 0~2)와 `blc-fix-resynthesis-2026-07-03.md`가
> 아래 1·2·4번을 이미 실행했다 — 완료 표시로 갱신하고, 오늘 새로 식별된 항목을 추가한다.

1. ~~PR 컨트롤러 설계·합성~~ **✅ 완료(1차)** — `pr_controller.v` FSM, trigger→완료
   1.716ms 시뮬레이션 확보(phase0-2-execution §2.1). 단 `drain_ready`가 여전히
   테스트벤치 임의 신호라 "1차"에 머무름(§4.4 갱신 필요).
2. ~~저조도 baseline core WB 완화 실험~~ **✅ 완료 + 정본 반영** — BLC가 진범으로
   확정(§1.2 갱신 필요, WB 아님), `BLC_OFFSET12_LOWLIGHT=128` 정본 반영 및 재합성까지
   완주(blc-fix-resynthesis §1~4).
3. **golden model 교차검증 CI 게이트 — ✅ 완료** — `verify_binning_cross_check.py`,
   `make verify`의 `cross-check` target에 통합, 과거 버그 재현 시 검출 확인
   (phase0-2-execution §0.1).
4. ~~pblock 재검토~~ **✅ 완료(단, 새 트레이드오프 발생)** — `X1Y0:X2Y0`으로 재floorplan,
   용량 2배. 그 대가로 partial bitstream·재구성 지연도 2.11배 증가(§4.2 갱신 필요).

**오늘(2026-07-03) 새로 식별된, 아직 미완료인 항목 — 우선순위 순:**

1. **신 pblock 기준 timing-constrained WNS 재검증** (§3.1) — 보드 불필요, 기존
   스크립트 재사용 가능. 가장 저비용.
2. **partition pin 15→3 원인 조사** (§4.5) — 보드 불필요, 기존 `.dcp` 재분석만 필요.
3. **PR 컨트롤러 `drain_ready`를 실제 RM `ap_idle`에 연결** (§4.4) — Stage 6 착수의
   실질적 선결 과제.
4. **checker leaf 임계값(`DARK_Y=50`) 및 복귀 임계값(`dark_ratio<0.20`) 재검증** (§1.5) —
   바뀌면 진입 임계값(0.80) 재스윕까지 연쇄되는 더 큰 작업이라 별도 세션 권장.
5. **COCO 포화율(saturation%) 재측정** (§1.4) — 최신 파라미터(gain2.0/BLC128)로
   `image_metrics` 재실행, 저비용.

## 산출물
이 문서는 기존 문서를 종합한 것으로 별도 코드/CSV 산출물 없음. 근거:
`experiment-report-2026-07-02.md`, `lowlight-rm-map-rootcause-2026-07-02.md`,
`stage4-hw-synthesis-2026-07-02.md`, `stage5-dfx-implementation-2026-07-02.md`,
`pr-latency-vivado-sim-2026-07-02.md`, `pblock_capacity.rpt`(2026-07-03 재확인),
`config{1,2}_impl.util.rpt`(§8 CONFIGURATION, 2026-07-03 재확인).
