<!--
=============================================================================
File   : isppipeline/hls/results/pr-latency-vivado-sim-2026-07-02.md
Date   : 2026-07-02
Time   : 23:10 KST
Function: Vivado(XSIM) 기반 ICAP partial-reconfiguration latency 실측 시도 -- 실제
          rm_lowlight_partial.bit를 Xilinx ICAPE3/SIM_CONFIGE3 UNISIM 동작 모델에
          직접 흘려서 trigger→완료 시점을 재는 테스트벤치를 만들고 5차례 반복
          실행했다. 결과와 한계를 있는 그대로 기록.
Goal   : "vivado 시뮬레이션 돌리면 dfx trigger 시점부터 전환 완료까지 latency를
         확인할 수 있잖아" 요청에 대한 응답 -- 실제로 시도했고, 무엇을 얻었고
         무엇을 얻지 못했는지 정직하게 기록한다(SPEC.md §11 "HW 수치 위조 금지").
=============================================================================
-->
# Vivado 시뮬레이션 기반 ICAP PR Latency 실측 시도 (2026-07-02)

## 1. 접근

`results/pr-latency-breakdown-2026-07-02.md`는 partial bitstream 크기 ÷ 데이터시트
ICAP 대역폭이라는 **순수 산술 추정**이었다. 이보다 나은 근거를 얻기 위해, Vivado가
제공하는 **ICAPE3 UNISIM 동작 시뮬레이션 모델**(`SIM_CONFIGE3` 내장 — 실제 Type1/
Type2 설정 패킷을 파싱하고 SYNC/DESYNC/CRC를 추적하는 패킷 레벨 파서)에 **오늘 생성한
실제 `rm_lowlight_partial.bit`(686,664 bytes)를 직접 스트리밍**하는 테스트벤치를 만들어
`xvlog`/`xelab`/`xsim`(Vivado Simulator)으로 5차례 반복 실행했다.

### 1.1 준비
- `.bit` 파일의 132-byte ASCII 헤더를 벗겨내 순수 바이너리 payload 추출(Python) —
  686,532 bytes = **171,633개의 32-bit word**. 이 숫자는 헤더에 내장된 길이 필드
  (`0x000A79C4` = 686,532)와 정확히 일치해 실측 검증됨.
- `rm_normal_partial.bit`도 동일하게 파싱한 결과 word 수·DESYNC 명령 위치가
  `rm_lowlight_partial.bit`와 **완전히 동일**(6414/6724/165183/171615) — 두 RM이
  같은 프레임 주소 범위(같은 pblock)를 대상으로 하므로 명령 구조 자체가 동일하고
  프레임 데이터 내용만 다르다는 것을 바이트 레벨에서 재확인.
- 테스트벤치: `ICAPE3` 인스턴스에 100MHz(AMD UG570 spec 상한) 클럭을 걸고, "DFX
  trigger" = 첫 번째 활성 ICAP write 사이클로 정의, 전체 171,633 word를 순서대로 흘림.

## 2. 결과 — 3단계 완료-감지 시도, 모두 동일한 한계에 도달

| 시도 | 방법 | 결과 |
|---|---|---|
| ① `ICAPE3.PRDONE` 출력 신호 대기 | 전체 171,633 word 스트리밍 후에도 **PRDONE이 끝내 assert 안 됨**(PRERROR=0) | 실패 |
| ② 실제 bitstream 바이트에서 DESYNC 명령(`0x30008001`→`0x0000000D`) 패턴 매칭 | 4곳에서 매칭(word 6414/6724/165183/171615) — **첫 매칭(6414)이 진짜 명령인지 payload 우연 일치인지 구분 불가**(순수 바이트 매칭의 근본 한계) | 신뢰 불가 |
| ③ `SIM_CONFIGE3` 내부의 실제 패킷 파서가 계산하는 `desync_flag` 신호를 계층 참조로 직접 관측(`dut.SIM_CONFIGE3_INST.desync_flag`) | SYNC는 **정확히 word index 2(t=1.32µs)에서 성공**(bitstream 구조 자체는 유효함을 확인) — 그러나 전체 171,633 word를 다 흘려도 **desync_flag가 끝내 `0000`(synced 상태)에 머무르고 `1111`(desync 완료)로 복귀하지 않음** | 실패 |

**공통 결론: 세 가지 독립적인 완료-감지 방법 모두 "격리된 ICAPE3 단독 테스트벤치"에서는
완료 신호를 얻지 못했다.** SYNC(word 2)까지는 정상 동작해 bitstream 자체의 구조적
유효성은 검증됐지만, 그 이후 완료 인식에는 이 테스트벤치가 제공하지 않는 무언가가
더 필요하다.

## 3. 왜 안 되는가 (원인 분석)

`SIM_CONFIGE3`의 `PRDONE`은 코드상 `(&desync_flag) & (&eos_startup)` 조건이다.
`eos_startup`("End Of Startup")은 디바이스 전체의 STARTUP 시퀀스(클럭 스위칭, GTS/GSR
해제 등, 실제로는 `STARTUPE3` 프리미티브가 관장)와 연결된 신호로 추정된다 — **부분
재구성은 이미 동작 중인 시스템에 대해 일어나므로 실제로는 전체 STARTUP 시퀀스를 다시
타지 않는다.** 즉 이 UNISIM 조합의 `PRDONE`은 **"완전한 디바이스 최초 설정" 시나리오를
전제로 설계된 신호**이고, `ICAPE3` 하나만 격리해 파샬 비트스트림을 흘리는 시나리오는
애초에 이 모델이 상정한 사용법이 아닐 가능성이 높다. `desync_flag` 자체가 끝까지
`0000`에 머문 것도 같은 계열의 문제로 보인다(내부 FDRI 프레임-카운트 북키핑이 완전한
디바이스 컨텍스트를 요구하거나, `DEVICE_ID`/`IDCODE` 검증 등 격리 테스트벤치에서
충족하지 못한 사전조건이 있을 가능성).

**이것은 테스트벤치 버그를 계속 고쳐서 해결할 문제라기보다, 이 시뮬레이션 방법론
자체의 한계로 판단한다** — 진짜로 신뢰할 수 있는 "trigger→완료" 신호를 시뮬레이션에서
얻으려면 static 영역 전체(AXI infra + PS 등가 모델 + `STARTUPE3`)를 포함한 완전한
디바이스 레벨 시뮬레이션 환경이 필요해 보이며, 이는 Stage 4 문서(§6b)가 이미 기록한
"이 WSL2+Vitis HLS 2024.1+XSIM 환경 특유의 cosim 하네스 한계"와 같은 계열의 문제다.

## 4. 그럼에도 이번 시도로 확보한 것 (헛되지 않은 결과)

1. **실제 payload word 수를 파일에서 직접·정밀하게 검증**(686,532 bytes = 171,633
   words, 헤더 내장 길이 필드와 정확히 일치) — 기존 `pr-latency-breakdown-2026-07-02.md`
   가 전체 파일 크기(686,664 bytes, 132-byte 헤더 포함)를 썼던 것보다 0.02%p 더
   정밀한 숫자.
2. **SYNC가 word index 2에서 정상 성공** — bitstream이 Xilinx 표준 설정 프로토콜상
   구조적으로 유효함을 Xilinx 자신의 파서로 재확인(간접적이지만 pr_verify PASS와
   더불어 이 bitstream이 "진짜로 올바르게 생성됐다"는 두 번째 독립 증거).
3. **두 RM의 partial bitstream이 명령 구조까지 완전히 동일**(word count·DESYNC
   후보 위치 4곳 모두 일치)함을 바이트 레벨에서 재확인 — "partial bitstream 크기는
   pblock 프레임 수로 결정되고 로직 사용량과 무관하다"(Stage 5 문서)는 주장을
   한 단계 더 깊은 근거로 뒷받침.
4. **완료 신호를 못 얻은 것 자체가 유효한 발견**: Stage 6(보드) 준비 시 "그냥 XSIM으로
   PR 컨트롤러를 시뮬레이션하면 되겠지"라는 가정이 틀렸음을 미리 발견 — 실제 보드
   실측 또는 훨씬 무거운 완전 디바이스 시뮬레이션 환경 구축이 필요함을 사전에
   확인했다(다음 단계 계획에 직접 반영 가능).

## 5. 결론 — 수치는 바뀌지 않는다, 근거의 정밀도만 올라갔다

시뮬레이션에서 신뢰할 수 있는 완료 신호를 얻지 못했으므로, latency 수치 자체는
`results/pr-latency-breakdown-2026-07-02.md`의 **peak 1.72ms / 전형 6.87ms** 추정을
유지한다. 다만 이번 시도로 그 계산에 쓰인 word 수(171,633)가 **추정이 아니라 실제
파일에서 직접 파싱·검증된 값**이라는 점, 그리고 "이보다 더 정확한 값을 시뮬레이션으로
얻으려는 시도는 이미 해봤고 왜 막혔는지"까지 문서화됐다는 점이 이번 작업의 진짜 성과다.

## 6. 재현

```bash
# 1) payload 추출 (132-byte 헤더 제거)
python3 -c "
data = open('rm_lowlight_partial.bit','rb').read()
open('rm_lowlight_partial.hex','w').write(
    '\n'.join(data[132+i:132+i+4].hex() for i in range(0, len(data)-132, 4)))
"
# 2) UNISIM 컴파일 + 테스트벤치 (icap_pr_latency_tb.v)
source /tools/Xilinx/Vivado/2024.1/settings64.sh
xvlog --nolog /tools/Xilinx/Vivado/2024.1/data/verilog/src/glbl.v
xvlog --nolog -sv icap_pr_latency_tb.v   # ICAPE3.v/SIM_CONFIGE3.v는 xvlog가 자동 검색
xelab --nolog icap_pr_latency_tb glbl -s icap_pr_latency_sim
xsim --nolog icap_pr_latency_sim -R
```
(hex 파일은 `.bit`에서 매번 재생성 가능한 파생물이라 repo에는 커밋하지 않음 —
`/tmp/hls_dfxisp/dfx/icap_sim/`에서 작업, 테스트벤치 소스만 아래 경로에 보존.)

## 산출물
- 코드: `results/icap_sim/icap_pr_latency_tb.v`
- 관련 문서: `results/pr-latency-breakdown-2026-07-02.md`(수치 자체는 이 문서로 유지)
