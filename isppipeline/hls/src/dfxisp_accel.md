# dfxisp_accel.cpp — 설계 배경·이력 노트

`dfxisp_accel.cpp`의 소스 주석에서 분리한 설계 배경 문서. 코드 동작을
바꾸는 내용은 없고, 상수·구조가 왜 지금 값/형태인지의 근거 기록이다.
(원 출처: haengmini/dfxisp 커밋 `ea6c2de`의 소스 헤더 주석)

## 1. 파일 개요

DFXISP core C-sim — 공유 baseline ISP core + 상호배타적 tone RM 슬롯.
정수 연산만 사용, Python golden model(`tools/gen_golden_vectors.py`, 원본
레포)과 비트 단위로 일치한다.

주요 갱신 이력:
- **2026-07-20** — BLC 재보정 16/8 → 2/2, checker C1 배포(DARK_RATIO_PCT 80→62)
- **2026-07-03** — low-light BLC 완화(후에 07-20 재보정으로 대체)
- **2026-07-02** — adversarial review 수정 2건 (§3)

## 2. BLC / checker 상수의 근거

### BLC_OFFSET12 = BLC_OFFSET12_LOWLIGHT = 2<<4 (black level 2)

2026-07-20 재보정(승인·배포됨). 실 센서 RAW 스윕(SonyNOD 321 + PASCALRAW
321, BLC ∈ {0,1,2,4,8,16}, 정본 gamma-2.0 파이프라인)에서 **모든 arm·모든
split의 mAP 피크가 BLC 1~2**에 있었다. 이전 값 16(normal)/8(low-light)은
야간 데이터에서 최대 5.7× mAP 손실. 두 모드 모두 측정 피크인 2를 공유하므로
2026-07-03의 모드별 완화는 대체됨.

경위: 2026-07-03 root-cause ablation이 "BLC/WB" 묶음을 BLC-only / WB-only /
WB-skip으로 분리해 **black-level offset이 원인이고 WB gain은 아님**을 규명
— BLC 단독 완화(16→8)만으로 ExDark mAP 0.062→0.150 회복(normal arm 대비
+42%), WB 완화/생략은 +7%/−0.5%로 무효과. COCO는 중립~+5%라 타깃 조건 밖
에서도 안전. 이 때문에 `apply_blc_wb12()`에 `blc_offset` 파라미터가 생겼고
WB/CCM은 두 모드가 문자 그대로 같은 코드 경로를 쓴다.

### DARK_RATIO_PCT = 62 (checker C1)

2026-07-20 배포(gate 4). **dark16 비율 > 0.62** 규칙이 2026-07-02의 C0
(dark50 > 0.80)을 대체. C1이 모든 지표에서 C0 우위: recall 0.936 vs 0.918,
false-trigger 0.089 vs 0.125, Youden J 0.847 vs 0.793. **RTL 변경 zero** —
dark-pixel 임계는 런타임 `dark_pixel_threshold` AXI-lite 레지스터라서:

- **드라이버는 이제 256을 써야 한다** (= 16<<4, 이 raw12 도메인 기준.
  데이터셋 raw16 표현에서의 등가값은 16<<8 = 4096).
- 컴파일 타임에는 비율 상수(62)만 바뀜.

실 센서 검증(gate 3): SonyNOD recall + PASCALRAW false-trigger
C0 92.9% → C1 41.8%. C1 스펙의 Schmitt 히스테리시스 밴드(Δ=2%p)는
드라이버 측 정책(모드 FF 1개)이고, 여기 단일-프레임 규칙은 순수 임계 비교로
유지된다.

## 3. Adversarial review 수정 2건 (2026-07-02)

(1) **Chroma-collapse 버그(고심각도)**: 구 low-light 프론트엔드는 2×2 셀의
RGGB 4샘플을 스칼라 하나로 평균한 뒤 그 그리드를 다시 Bayer인 것처럼
재-demosaic했다 — 모든 "픽셀"이 이미 R+2G+B 혼합 평균이라 chroma가 AWB/gain
이전에 구조적으로 파괴됐다. 더 나쁜 건 golden model이 같은 버그를 미러링해
`make verify`의 bit-exact 검사가 이를 잡을 수 없었다는 것(HLS가 자신의 잘못된
golden과 완벽히 일치). 반면 SW mAP 근거는 chroma 보존 알고리즘
(R=좌상, G=avg(우상,좌하), B=우하)을 쓰고 있어 보고된 low-light mAP는 이
파일이 실제 계산하는 것의 근거가 아니었다.
**수정**: `compute_binned_rgb_row()`가 2×2 binning+demosaic을 한 단계로
융합해 채널별 R/G/B를 직접 추출(SW 구현과 bit-exact)한 뒤 공유
`apply_blc_wb12()`로 직행 — 두 번째(Bayer 가정) demosaic 패스 제거. 이로써
3-row 슬라이딩 윈도우 계열 헬퍼가 통째로 불필요해짐(binning-demosaic은 자기
셀의 raw 2행만 필요).

(2) **메타데이터 RTL 가시성(중심각도)**: `DfxIspResult*` 구조체 포인터를
`s_axilite`로 선언했었는데, 이는 경량 슬레이브 레지스터 접근용이지 메모리
기록형 구조체 출력용이 아니며, 4개 필드가 개별 주소의 레지스터로 합성된다는
근거 아티팩트가 없었다. 4개의 개별 스칼라 `int*` 출력 포인터
(out_width/out_height/selected_mode/selected_rm)로 교체 — post-completion
status 레지스터의 표준 Vitis HLS 관용구(rm_*_top들과 같은 패턴).

## 4. 초기(ver1) 이력

- RAW-domain-first 재배열: baseline core = demosaic→BLC→WB→CCM (12-bit),
  RM_NORMAL_TONE은 identity가 아니라 gain+gamma, RM_LOW_LIGHT_TONE은
  gain 2.0x + gamma.
- `gamma2()`: 런타임 Newton isqrt(csynth 자원 지배 — run_low_light 기준
  28.6k FF / 25.4k LUT)에서 Python으로 1회 생성한 256-entry ROM 테이블로
  이동. `std::array`/constexpr을 먼저 시도했으나 Vitis HLS 동봉 gcc-8.3.0
  STL이 `-std=c++17`에서 `<array>`를 거부해 plain C array 사용.
- Bayer 패턴 RGGB (SW 데이터셋과 통일).

**순서 규칙**: tone RM 슬롯이 공유 baseline core를 감싼다. low-light
binning-demosaic은 RAW에서(정밀도 손실 전에) 수행하고, gain/gamma는 tone
스테이지에만 존재한다(baseline core 내 중복 금지 — de-dup rule).

## 5. 독립 RM top 2개 (rm_normal_tone_top / rm_low_light_tone_top)

Stage 5 준비(2026-07-02): 각 DFX Reconfigurable Module 후보가 **자기만의
자원/타이밍 수치**를 갖도록 별도 합성 가능한 entry point로 분리(통합
`dfxisp_accel()` top 내부의 서브 인스턴스 분해만으로는 부족 — SPEC.md §10).
같은 translation unit이라 익명 네임스페이스 헬퍼를 직접 호출하므로 동작은
bit-exact 동일. 합성 전용(csynth "synth" flow, 테스트벤치 없음)이고
`make verify`는 이들을 검증하지 않는다.

**포트 리스트는 두 top이 정확히 일치해야 한다**(인자 타입/순서/개수) —
DFX는 한 Reconfigurable Partition의 RM 구현들끼리 동일한 포트 리스트를
요구한다("동일 downstream 인터페이스 계약"). out_width/out_height는
rm_normal_tone_top에서 항상 width/height와 같다(형상 보존).

## 6. cosim `depth=` 힌트의 사연

`#pragma HLS INTERFACE m_axi ... depth=2048`의 `depth=`는 C/RTL cosim의
m_axi bus functional model 메모리 사이징 힌트일 뿐, 합성 RTL 동작에는
영향이 없다(실제 depth는 런타임 width*height).

- `depth=1920*1080`(설계 전체 envelope): ENTER_WRAPC에서 SIGSEGV
  (wrapc 하네스 스택 오버플로 추정)
- `depth=1024`: 통과했으나 테스트 7 트랜잭션 모두 완료 후
  ENTER_WRAPC_PC(post-check)에서 SIGSEGV — 한 세션의 **누적** 주소 범위
  (7회 × 최대 256px ≈ 1800)보다 작았던 탓으로 추정
- `depth=2048`: 현재 픽스처 기준 여유를 두고 채택
