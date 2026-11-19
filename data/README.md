# 파라미터 데이터

모든 계수, 곡선, 작물 요구량, 비료 정의는 이 폴더의 TOML 파일에 둬요. 형식 규칙은 [ADR-0002](../docs/decisions/0002-data-format.md)를 따라요.

## 규칙

- 키 이름에 단위를 붙여요. 예: `field_capacity_mm`, `base_rate_per_hour`
- 구간 선형 곡선은 `[[x, y], ...]` 배열로 쓰고, x는 오름차순이에요.
- 값마다 출처를 주석으로 달아요. 출처가 없으면 `# 게임용 임의값`이라고 적어요.
- 파라미터 변경은 별도 PR로 올리고, 변경 이유를 PR 설명에 남겨요.

## M5a (schema_version 6)

- `params.toml` schema 6: `[microbes]`(공통) + `[microbes.<id>]` 7개, `[soil_disease.<id>]`, `[disinfection]`, `[[mulch]]`, `[[disinfectant]]`, `[[inoculant]]`, `[root_damage]`의 감염·약제 곡선. 쓰지 않는 곡선 축은 `[[0, 1], [14, 1]]`처럼 명시해요(숨은 기본값 없음). `competition`은 `{ 무리id = α }` 인라인 테이블이고 적지 않은 무리는 0이에요.
- 시나리오 schema 6: `[microbes]`(필수, 무리 7개 시작 지수). 일정표 동작 `set_mulch`(`mulch`), `apply_disinfectant`(`disinfectant`, `g_per_m2`), `add_inoculant`(`inoculant`, `g_per_m2`), `plant`(`seedling_grade`, `plants_per_m2`, 하우스 전체라 `area` 없음).
- `scenarios/m5a/`는 `tools/gen_m5a_scenarios.py`가 만들어요 (README에 실행 일수 표).
