<!--
=============================================================================
File   : README.md
Date   : 2026-08-06 KST
Function: JNU_DFXISP_FPGA — 원본 연구 레포(haengmini/dfxisp)에서 FPGA 실험에
          필요한 것만 추린 인수인계용 레포: HLS 소스, DFX 플로우 스크립트,
          실 RAW 샘플 데이터(PASCALRAW·Sony NOD), SPEC. FPGA 실험 담당자가
          이 레포만 clone해서 Stage 6(보드) 작업을 시작할 수 있도록 구성.
Origin : https://github.com/haengmini/dfxisp (커밋 ea6c2de 기준, 2026-08-06 추출)
=============================================================================
-->
# JNU DFXISP — FPGA 실험 레포

원본 연구 레포 [haengmini/dfxisp](https://github.com/haengmini/dfxisp)에서
**FPGA 실험에 필요한 것만** 추린 레포입니다: HLS 소스, Vivado DFX 플로우,
실 RAW 샘플 데이터, 설계 스펙(SPEC.md). ML 평가 도구·중간 리포트·전체
데이터셋은 원본 레포에 있습니다.

## 타깃 / 도구

| 항목 | 값 |
|---|---|
| 보드 | ZCU104 (Zynq UltraScale+ `xczu7ev-ffvc1156-2-e`) |
| 도구 | Vivado / Vitis HLS **2024.1** |
| 타깃 클럭 | 5.0 ns (200 MHz) |
| 현재 단계 | Stage 4~5 완료(HLS 합성·DFX 구현, fabric-only), **Stage 6(보드) 미착수** |

## 디렉터리 구조

```
SPEC.md                          # 설계 스펙 정본 — §6~7 구조/RP 경계, §10 자원/타이밍 실측
isppipeline/hls/
  src/, include/                 # HLS C++ 소스 (dfxisp_accel = static+RP, dfxisp_rm = RM들)
  tests/                         # csim 테스트벤치 + golden vector CSV (커밋돼 있음)
  scripts/vitis_hls.tcl          # HLS csynth/cosim 플로우
  scripts/dfx/                   # Vivado DFX 플로우 (dfx_flow.tcl 등)
  results/pr_controller/         # PR 컨트롤러 RTL + TB (Stage 6 선결 과제 대상)
  results/icap_sim/              # ICAP PR 레이턴시 시뮬 TB
data/                            # 실 RAW 샘플 (아래 "데이터셋" 절)
```

`scripts/dfx/dfx_flow.tcl`은 스크립트 위치 기준 4단계 상위를 repo root로
계산하므로 디렉터리 구조를 원본과 동일하게 유지했습니다 — 스크립트 수정
없이 그대로 동작합니다.

소스의 긴 설계 배경 주석은 **같은 폴더의 동명 `.md` 노트로 분리**돼
있습니다 (`src/dfxisp_accel.md`, `include/dfxisp_accel.md`,
`results/pr_controller/pr_controller.md`,
`results/icap_sim/icap_pr_latency_tb.md`). 코드에는 짧은 요약과 해당 노트
포인터만 남아 있습니다.

## 데이터셋 — 입력은 실 RAW (pseudo-RAW 아님)

파이프라인 입력은 카메라 ISP를 거치지 않은 **실 RAW 센서 데이터**입니다.
두 공개 데이터셋을 사용합니다:

| 데이터셋 | 카메라 | 조도 | 이 레포의 샘플 |
|---|---|---|---|
| **PASCALRAW** (Omid-Zohoor et al., Stanford Digital Repository) | Nikon D3200, 12-bit | 100% 주간 | `data/pascalraw_test/` 3프레임 |
| **Sony NOD** (RAW-NOD, Night Object Detection) | Sony RX100 VII, RGGB | 저녁/야간 | `data/sonynod_test/` 3프레임 |

각 샘플 디렉터리 구성:

- `raw_bin/*.bin` — 16-bit 컨테이너에 담긴 RAW Bayer 프레임.
  스케일은 `shift8` (`round(linear*255)<<8`), Bayer/black/white level은
  `meta.json`·`frames_meta.csv` 참조 (PASCALRAW: black 0/white 4095,
  SonyNOD: black 800/white 16380 원본을 변환한 값 — 반드시 meta 확인).
- `images/*.jpg` — 참조용 렌더 이미지 (RAW가 아님, 눈으로 확인용).
- `labels/*.txt` — YOLO 포맷 검출 라벨 (보드 실험엔 불필요, 참고용).
- `meta.json` — 변환 파라미터 정본.

**전체 데이터셋** (PASCALRAW 4,259 / SonyNOD 321 프레임, 수백 GB)은 GitHub
용량 제한으로 여기 못 올립니다. 필요하면:

1. 공식 배포처에서 다운로드 — PASCALRAW는 Stanford Digital Repository,
   RAW-NOD는 공개 저장소에서 배포.
2. RAW → `raw_bin` 변환 도구는 원본 레포 `isppipeline/hls/tools/`에 있음
   (변환 파라미터는 각 `meta.json`에 기록된 것과 동일하게).
3. 또는 데스크탑에 변환 완료본이 있으니 외장 디스크/Drive로 전달 요청.

## 빌드 / 실행

bitstream·DCP·RTL(build/)은 git에 없습니다. 재생성:

```bash
cd isppipeline/hls
make csim rm-csim          # C 시뮬 — golden CSV 대조, 바로 실행 가능
# csynth/cosim: vitis_hls -f scripts/vitis_hls.tcl
#   주의 — 중첩 프로젝트 경로에서 add_files 하면 링크 에러
#   (undefined symbol: dfxisp_accel)가 나는 기존 버그가 있음.
#   hpp/cpp/tb를 평평한 임시 디렉터리(예: /tmp/hls_dfxisp/)에 복사해
#   그 안에서 vitis_hls를 실행할 것.
# DFX:  vivado -mode batch -source scripts/dfx/dfx_flow.tcl
```

`make verify` 등 golden 재생성 타깃은 원본 레포의 `tools/` Python이 필요해
이 레포에서는 동작하지 않습니다 (csim은 커밋된 golden CSV로 충분).

**주의(실제 사고 사례): HLS가 생성한 RTL을 다른 곳으로 복사할 때 `cp *.v`만
하면 감마 ROM `.dat` 파일이 빠져 합성 결과가 달라집니다** — `.dat`을 반드시
함께 복사. 또한 **파티션 빌드와 flat 빌드의 자원 수치를 섞어 비교하지 말
것**(동일 넷리스트에서 24% 차이, 원인 미규명 — SPEC.md §10). 자원 비교는
flat 축, 파티션 빌드는 bitstream/pr_verify 용도.

## 해야 할 일 — Stage 6 (보드)

순서대로:

1. **선결 과제**: PR 컨트롤러(`isppipeline/hls/results/pr_controller/
   pr_controller.v`)의 `drain_ready`를 실제 RM `ap_idle`에 연결,
   ICAPE3/STARTUPE3 인스턴스화, BRAM 시뮬레이션 소스를 실제 SD/DDR 경로로
   교체.
2. PS/DDR 통합 (Vivado Block Design).
3. 신 pblock 기준 clock/reset 핀 배정 + WNS 재검증.
4. (선택) partition pin 수 15→3 감소 원인 조사 (SPEC.md §10에 미조사로 기록).
5. (선택) cosim 완주 — WSL2+XSIM 환경의 자동 post-check SIGSEGV는 문서화된
   기존 버그이고 RTL 시뮬 자체는 10/10 통과. 네이티브 리눅스/윈도우
   Vivado에서는 재현 안 될 수 있음.

## 결과 기록 원칙

측정하지 않은 수치를 추정으로 적지 않습니다(SPEC.md §11.4). 기대와 다른
결과가 나와도 지우지 말고 그대로 기록합니다.
