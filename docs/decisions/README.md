# 결정 기록 (ADR)

무엇을 왜 결정하고 단순화했는지 남겨요. 한 번 승인된 ADR은 고치지 않고, 바꿀 때는 새 ADR을 써서 이전 것을 "대체됨"으로 표시해요.

## 상태 값

**제안됨** → (디렉터 검토) → **승인** 또는 **기각**. 나중에 바뀌면 **대체됨 (ADR-XXXX)**.

## 목록

| 번호 | 제목 | 상태 |
| --- | --- | --- |
| [0001](0001-game-engine.md) | 게임 엔진으로 Godot 4 채택 | 승인 |
| [0002](0002-data-format.md) | 파라미터 데이터 포맷으로 TOML 채택 | 승인 |
| [0003](0003-test-framework.md) | 테스트 프레임워크로 Catch2 v3 채택 | 승인 |
| [0004](0004-determinism-scope.md) | 결정론 보장 범위와 규칙 | 승인 |
| [0005](0005-cpp-toolchain.md) | C++ 표준과 툴체인 | 승인 |
| [0006](0006-off-season-and-disinfection.md) | 하계 휴작기 정비와 토양 소독, 육묘 제외 | 승인 |
| [0007](0007-acid-toxicity-and-manganese.md) | 산성 장해는 곡선 근사, 망간은 별도 풀 | 승인 |
| [0008](0008-cation-exchange-model.md) | 양이온 교환은 "빈자리 경쟁 + 완화"로 근사 | 승인 |
| [0009](0009-no-fixed-recipe-variation.md) | "항상 같은 정답"을 막는 변동 설계 | 승인 |
| [0010](0010-manganese-oxide-pool.md) | 망간에 산화물 풀과 pH 의존 흡착력 추가 (ADR-0007 2번 보완) | 승인 |
| [0011](0011-house-climate-in-m4.md) | 하우스 기후 관리(다중 피복·측창·난방·CO2)를 M4로 당김 | 승인 |
| [0012](0012-m5-split-and-population-index.md) | M5를 M5a·M5b로 나누고 개체군을 0~1 상대 지수로 | 승인 |
| [0013](0013-m5a-deep-soil-and-cell-variation.md) | M5a 깊은 흙 온도 항과 셀 간 병해 차이 | 승인 |
| [0014](0014-m5b-transpiration-humidity-feedback.md) | 증산의 습도 되먹임을 M5b로 당김 (ADR-0011 결정 6 일부 대체) | 제안 |

## 템플릿

```markdown
# ADR-XXXX: <제목>

- 상태: 제안됨
- 날짜: YYYY-MM-DD

## 맥락
## 결정
## 근거
## 결과 (좋은 점, 감수하는 점)
## 검토한 대안
```
