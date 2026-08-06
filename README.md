<!--
=============================================================================
File   : README.md
Date   : 2026-08-06 KST
Function: JNU_DFXISP_FPGA — 원본 연구 레포(haengmini/dfxisp)에서 FPGA/HW
          트랙(HLS·Vivado DFX·보드 실험)에 필요한 파일만 추린 인수인계용
          레포. FPGA 실험 담당자가 이 레포만 clone해서 Stage 6(보드) 작업을
          시작할 수 있도록 구성.
Origin : https://github.com/haengmini/dfxisp (커밋 ea6c2de 기준, 2026-08-06 추출)
=============================================================================
-->
# JNU DFXISP — FPGA 실험 레포

원본 연구 레포 [haengmini/dfxisp](https://github.com/haengmini/dfxisp)에서
**FPGA/하드웨어 트랙만** 추린 레포입니다. ML 평가(mAP 캠페인), 데이터셋,
모델 가중치는 여기 없고 원본 레포에 있습니다.

## 타깃 / 도구

| 항목 | 값 |
|---|---|
| 보드 | ZCU104 (Zynq UltraScale+ `xczu7ev-ffvc1156-2-e`) |
| 도구 | Vivado / Vitis HLS **2024.1** |
| 타깃 클럭 | 5.0 ns (200 MHz) |
| 현재 단계 | Stage 4~5 완료(HLS 합성·DFX 구현, fabric-only), **Stage 6(보드) 미착수** |

## 먼저 읽을 문서 (이 순서대로)

1. **`isppipeline/hls/results/HW-INTERFACE-PIN-MODULE-PROTOCOL-2026-08-03.md`**
   — 핀 매핑·모듈 관계·통신 프로토콜 브리핑. 처음 보는 사람용으로 작성됨.
   "0. 결론부터"만 읽어도 큰 그림이 잡힘.
2. **`SPEC.md`** — 설계 정본 스펙. 특히 §6~7(구조·RP 경계), §10(자원/타이밍
   실측), §11(연구 원칙 — **HW 수치 위조 금지**: 측정 안 된 값을 사실처럼
   적지 않는다).
3. **`isppipeline/hls/README.md`** — HLS 빌드/검증 실행법.
   **"C-synthesis / Co-sim 실행 노트" 절 필독** — 중첩 프로젝트 경로에서
   `add_files` 하면 링크 에러(`undefined symbol: dfxisp_accel`)가 나는 기존
   버그가 있어, hpp/cpp/tb를 평평한 임시 디렉터리에 복사해 돌리는 우회법을
   써야 함.
4. `isppipeline/hls/results/dfx-reimplementation-2026-08-01.md` — 최신 DFX
   재구현 기록(현재 배포 상수 BLC 2/2·checker C1 반영본).

## 디렉터리 구조

```
SPEC.md                          # 설계 스펙 정본
isppipeline/hls/
  src/, include/                 # HLS C++ 소스 (dfxisp_accel = static+RP, dfxisp_rm = RM들)
  tests/                         # csim 테스트벤치 + golden vector CSV (커밋됨)
  scripts/vitis_hls.tcl          # HLS csynth/cosim 플로우
  scripts/dfx/                   # Vivado DFX 플로우 (dfx_flow.tcl 등)
  tools/                         # Makefile 검증 타깃이 쓰는 Python 참조모델만 발췌
  reports/csynth/, postroute/    # 실측 리포트 (비교 기준치)
  results/                       # HW 관련 문서·RTL (pr_controller.v, ICAP 시뮬 TB 등)
```

`scripts/dfx/dfx_flow.tcl`은 스크립트 위치 기준 4단계 상위를 repo root로
계산하므로 **디렉터리 구조를 원본과 동일하게 유지**했습니다. 이 레포 어디서든
원본과 같은 명령으로 동작합니다.

## 빌드 산출물 재생성

bitstream/DCP/RTL(build/)은 git에 없습니다. 재생성 순서:

```bash
cd isppipeline/hls
make csim rm-csim          # C 시뮬 (golden CSV 대조, 도구 없이 즉시 가능)
# csynth: README.md의 flat-tempdir 노트대로 vitis_hls -f scripts/vitis_hls.tcl
# DFX:    vivado -mode batch -source scripts/dfx/dfx_flow.tcl
```

**주의(사고 사례): RTL을 다른 곳으로 복사할 때 `cp *.v`만 하면 감마 ROM
`.dat` 파일이 빠져 합성 결과가 달라집니다** — `.dat`을 반드시 함께 복사.
또한 **파티션 빌드와 flat 빌드의 자원 수치를 섞어 비교하지 말 것**(동일
넷리스트에서 24% 차이, 원인 미규명 — SPEC.md §10). 자원 비교는 flat 축,
파티션 빌드는 bitstream/pr_verify 용도.

## 해야 할 일 — Stage 6 (보드)

원본 ROADMAP 기준, 순서대로:

1. **선결 과제**: PR 컨트롤러(`results/pr_controller/pr_controller.v`)의
   `drain_ready`를 실제 RM `ap_idle`에 연결, ICAPE3/STARTUPE3 인스턴스화,
   BRAM 시뮬레이션 소스를 실제 SD/DDR 경로로 교체.
2. PS/DDR 통합 (Vivado Block Design).
3. 신 pblock 기준 clock/reset 핀 배정 + WNS 재검증.
4. (선택) partition pin 수 15→3 감소 원인 조사 (SPEC.md §10에 미조사로 기록).
5. (선택) cosim 완주 — WSL2+XSIM 하네스 SIGSEGV(기존 문서화된 버그,
   `results/cosim-waveform-analysis-2026-07-03.md`) 탓에 자동 post-check만
   실패했고 RTL 시뮬 자체는 10/10 통과. 네이티브 리눅스/윈도우 Vivado에서는
   재현 안 될 수 있음.

## 결과 기록 원칙

측정하지 않은 수치를 추정으로 적지 않습니다(SPEC.md §11.4). 기대와 다른
결과가 나와도 지우지 말고 그대로 기록합니다.
