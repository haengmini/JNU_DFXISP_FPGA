# pr_controller.v — 설계 배경 노트

`pr_controller.v` 소스 주석에서 분리한 문서. (원 출처: haengmini/dfxisp
커밋 `ea6c2de`)

## 무엇인가

첫 번째 DFX partial-reconfiguration 컨트롤러 FSM (Phase 2.1).

```
FSM: IDLE -> DRAIN_WAIT -> ICAP_ARM -> ICAP_STREAM -> DONE -> IDLE
```

## 왜 완료 신호를 자체 word counter로 만드나

config1/config2 utilization 리포트(2026-07-03)에는 ICAPE3/STARTUPE3
인스턴스가 **0개** — PR 컨트롤러가 아직 없었고, 이 때문에 고립
ICAPE3-only 시뮬레이션은 신뢰할 완료 신호를 만들 수 없었다: ICAPE3의
PRDONE은 eos_startup에 의존하고, 그건 실제 STARTUPE3/디바이스 수준
컨텍스트를 요구한다(이 설계에 없음).

이 모듈은 그 문제를 우회한다 — ICAPE3의 PRDONE을 신뢰하는 대신, **자체
word counter가 알려진 partial bitstream word 수(171,633 words — 실제
`rm_lowlight_partial.bit`를 파싱해 2026-07-02 검증)에 도달하면 완료**로
처리한다. 이 신호는 설계가 완전히 소유하므로 STARTUPE3 없이 시뮬레이션에서
검증 가능하다.

## Fabric-only 버전의 한계 (= Stage 6 선결 과제)

- partial bitstream 소스가 **BRAM**이다(시뮬레이션에선 `$readmemh`, 실사용
  가정은 BRAM init) — PS/DDR이 아직 아님. PS/DDR 업그레이드는 PS/DDR 통합
  이후의 단계.
- `drain_ready`는 실제 RM `ap_idle`에 아직 연결되지 않음.
- ICAPE3/STARTUPE3 미인스턴스화.

셋 다 이 레포 README의 "Stage 6 선결 과제" 항목이다.
