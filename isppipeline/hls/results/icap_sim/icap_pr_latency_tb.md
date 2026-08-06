# icap_pr_latency_tb.v — 측정 방법 노트

`icap_pr_latency_tb.v` 소스 주석에서 분리한 문서. (원 출처:
haengmini/dfxisp 커밋 `ea6c2de`)

## 무엇을 측정하나

PR(partial reconfiguration) 레이턴시 테스트벤치: 실제
`rm_lowlight_partial.bit` payload를 Xilinx ICAPE3 UNISIM behavioral
model에 정격 최대 클럭(100 MHz, AMD UG570)으로 흘리면서, "DFX trigger"
펄스(첫 active ICAP write)부터 **실제 DESYNC 커맨드**(Type1 write
CMD=0x0000000D, opcode 0x30008001)가 스트림에 나타나는 시점까지의
시뮬레이션 시간을 잰다.

## 왜 PRDONE이 아니라 DESYNC 검출인가

ICAPE3 자체의 PRDONE 출력(내장 SIM_CONFIGE3 패킷 파서 경유)을 먼저
시도했으나 고립 ICAPE3-only 테스트벤치에서는 절대 assert되지 않는다 —
PRDONE은 eos_startup을 요구하고, 그건 full device
STARTUPE3/configuration-sequence 시뮬레이션 컨텍스트가 필요하다. 이는
테스트벤치 버그가 아니라 고립 ICAP 시뮬레이션에 대한 UNISIM 모델의 실제
한계다.

대안으로 스트림 내용에서 실제 DESYNC 커맨드를 직접 검출한다 — DESYNC는
실제 bitstream에서 configuration sequence를 끝내는 바로 그 커맨드이고,
후처리로 독립 확인한 위치는 **두 partial bitstream 모두 word index
171,615 (전체 171,633 words)**로 동일했다. 두 RM이 같은 frame-address
커맨드 구조를 공유하고 frame data 내용만 다르다는 방증이기도 하다.
