# dfxisp_accel.hpp — 인터페이스 설계 노트

`dfxisp_accel.hpp`의 소스 주석에서 분리한 문서. API 사용에 필요한 최소
정보(픽셀 포맷·용량 규칙)는 헤더에 남아 있고, 여기는 경로 다이어그램과
설계 근거다. (원 출처: haengmini/dfxisp 커밋 `ea6c2de`)

## 아키텍처 — 두 처리 경로

공유 baseline ISP core + 상호배타적 모드별 tone RM. tone RM 슬롯이 공유
baseline core를 *감싼다*:

```
NORMAL:
  raw -> demosaic (RGGB)
      -> baseline_isp_core (BLC + WB + CCM, gain/gamma 없음)
      -> RM_NORMAL_TONE (gain 1.25x + gamma 2.0)
      -> RGB32  (H x W)

LOW_LIGHT:
  raw -> RM_LOW_LIGHT_TONE.front (2x2 RAW binning-demosaic, 융합 단계:
         이것이 binned 그리드의 demosaic 그 자체 — 채널 정체성 보존,
         R=좌상, G=avg(우상,좌하), B=우하. 4샘플 스칼라 평균 후
         재-demosaic 방식이 아님: 그건 chroma를 붕괴시킴)
      -> baseline_isp_core (BLC + WB + CCM — NORMAL 경로와 같은 함수,
         full-res demosaic 출력 대신 binned RGB에 적용)
      -> RM_LOW_LIGHT_TONE.back (gain 2.0x + gamma 2.0)
      -> RGB32  (H/2 x W/2, 형상 변경 Policy A)
```

C-sim golden gate가 증명하는 불변식:
- 프레임당 tone RM은 정확히 1개(상호배타)
- gain/gamma는 tone RM에만 존재, baseline core에 중복 없음
- 출력 메타데이터는 mode / selected RM / 출력 형상을 보고

## 입력 데이터에 대하여

입력은 **12-bit RAW Bayer RGGB** (uint16_t 컨테이너)다. 실험 입력은 실
센서 RAW의 변환본(PASCALRAW — Nikon D3200, Sony NOD — RX100 VII; 이 레포
`data/` 샘플과 그 `meta.json` 참조)이고, csim 테스트벤치는 합성 벡터
(`tests/golden_vectors.csv`)를 쓴다. 초기 문서에 있던 "pseudo-RAW" 표현은
초기 개발기의 합성/유사 RAW 벡터를 가리키던 것으로, 현재 실험 체계의
입력은 실 RAW다.

## 메타데이터 인터페이스 설계 근거 (2026-07-02 adversarial review)

메타데이터 출력을 단일 `DfxIspResult*` 구조체 포인터(s_axilite)에서 4개의
개별 스칼라 `int*` 출력 포인터로 교체했다. 구조체 포인터 over s_axilite는
검증되지 않은 HLS 패턴이고(s_axilite는 슬레이브 전용 컨트롤 인터페이스,
메모리 기록 마스터가 아님), 구조체 필드가 개별 주소 레지스터로 합성된다는
아티팩트(인터페이스 리포트·cosim)가 없었다. 개별 스칼라 출력 포인터는
post-completion status/readback 레지스터의 정착된 Vitis HLS 관용구다.
어느 포인터든 null일 수 있다(해당 필드 기록 생략).

같은 리뷰에서 헤더 서술 드리프트도 정정: RM_NORMAL_TONE은 "identity
bypass"가 아니고(ver1에서 gain+gamma 추가됨), RM_LOW_LIGHT_TONE의 tone은
gamma-2.0이다(gamma-4.0 아님).
