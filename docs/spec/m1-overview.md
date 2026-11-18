# M1 개요: 질소·수분·온도

- 상태: 승인 (2026-11-18)
- 마일스톤: M1
- 관련 명세: [soil-water](soil-water.md), [soil-temperature](soil-temperature.md), [organic-matter](organic-matter.md), [nitrogen](nitrogen.md), [core-api](core-api.md)
- 완료 기준(계획서 7장): 시나리오 S1·S2·S3 통과, 질소 보존 테스트 통과

이 문서는 M1 프로세스들이 공유하는 틀(단위, 셀 상태, 틱 순서, 명령, 장부, 데이터 배치)을 정해요. 각 프로세스의 수식과 인과관계는 개별 명세에 있어요.

## 플레이어 판단 (디자인 원칙 1)

M1이 만드는 판단은 세 가지예요.

- **퇴비 숙성도 선택**: 미숙 퇴비는 값싸고 질소가 빨리 나오지만, 발열과 암모늄 급증을 일으켜요. (S1)
- **비료 형태 선택**: 알칼리 토양에 암모늄계 비료를 주면 아질산이 쌓여요. 같은 질소량이라도 형태가 결과를 바꿔요. (S2)
- **관수량 조절**: 물을 많이 주면 질산태 질소가 씻겨 나가요. 비료를 줬는데 질소가 부족해지는 원인이 돼요. (S3)

## M1에서 다루지 않는 것 (해당 마일스톤까지 계수 1 또는 고정값)

| 요소 | M1 처리 | 도입 |
| --- | --- | --- |
| 통기성·산소 계수 | 1로 고정 | M2 |
| pH 변화 | 셀마다 시나리오 초기값으로 **고정**. 질산화의 pH 계수에는 이미 쓰여요 (S2에 필요) | M2 |
| EC, 토양 용액 이온 | 없음 | M2 |
| 양이온 교환 | NH4+는 "용액 비율" 계수 하나로 용탈을 줄여요 | M3 |
| 탈질, 암모니아 휘산 | 없음 (기체 손실 0) | M4 이후 |
| 미생물 개체군 | 분해균 활성 계수 1로 고정 | M5 |
| 작물 흡수 | 없음 (작물이 없어요) | M2 |
| 하우스 환경 제어(환기, 보온, 난방) | 하우스 기온은 외기와 일사로만 정해요 | M7/M8 |

## 단위 규약

토양 셀 하나는 **베드 면적 1 m², 근권 깊이 한 층**을 대표해요. 모든 양은 면적당 값이에요.

| 양 | 단위 | 접미사 예 |
| --- | --- | --- |
| 물 | mm (= L/m²) | `water_mm` |
| 탄소, 질소 풀 | g/m² | `labile_C_g_per_m2`, `nh4_N_g_per_m2` |
| 온도 | °C | `soil_temp_degC` |
| 속도 | 시간당 비율 | `decay_rate_per_hour` |
| 퇴비 투입량 | kg/m² (생체 기준) | `kg_per_m2` |
| 비료 투입량 | g/m² (제품 기준) | `g_per_m2` |

플레이어에게 보여줄 mg/kg 같은 검정 단위 환산은 측정 기능(M7)에서 다뤄요. 질소 풀은 모두 **질소 원소 기준(N)**이에요.

## 토양 셀 상태 (M1)

| 이름 | 의미 | 단위 | 명세 |
| --- | --- | --- | --- |
| `water_mm` | 토양 수분량 | mm | soil-water |
| `soil_temp_degC` | 토양 온도 | °C | soil-temperature |
| `labile_C_g_per_m2`, `labile_N_g_per_m2` | 이분해성 유기물 풀 | g/m² | organic-matter |
| `stable_C_g_per_m2`, `stable_N_g_per_m2` | 난분해성(부식) 유기물 풀 | g/m² | organic-matter |
| `nh4_N_g_per_m2` | 암모늄태 질소 | g N/m² | nitrogen |
| `no2_N_g_per_m2` | 아질산태 질소 | g N/m² | nitrogen |
| `no3_N_g_per_m2` | 질산태 질소 | g N/m² | nitrogen |
| `ph` | 토양 pH (M1에서는 고정) | - | nitrogen |
| `nitrite_alarm` | 아질산 임계치 초과 상태 (이벤트 중복 방지) | bool | nitrogen |
| `drained_mm_this_tick` | 이번 틱 배수량 (용탈 단계에서 사용, 매 틱 초기화) | mm | soil-water |

토양 고유 성질(`field_capacity_mm`)은 시나리오에서 오는 셀별 정적 값이에요.

## 틱 처리 순서 (M1)

계획서 4장 의사코드에서 M1 해당 부분만 채운 순서예요.

```
매 틱마다:
  1. 명령 큐의 행동을 적용한다            (관수, 퇴비, 비료 → 각 풀에 즉시 더함, 장부 기록)
  2. 하우스 기온을 갱신한다               (soil-temperature)
  3. 각 토양 셀에 대해 (행 우선 순서):
     a. 수분: 증발 → 배수량 계산          (soil-water)
     b. 온도: 하우스 기온 쪽으로 완화      (soil-temperature)
     c. (통기성: M2)
     d. (미생물: M5)
     e. 유기물 분해 → 발열을 토양 온도에 더함 (organic-matter)
     f. 질산화 1단계 → 2단계               (nitrogen)
     g. (양이온 교환·pH: M2/M3, 흡수: M2)
     h. 용탈: 배수량 비율만큼 이온 제거     (nitrogen)
     i. 아질산 임계치 판정 → 이벤트        (nitrogen)
  4. 기록용 스냅샷은 호출자가 snapshot()으로 읽는다
```

- 각 프로세스는 "이번 틱의 변화량"을 계산하고, 실제 이동량은 원천 풀의 남은 양으로 제한해요.
- 모든 1차 반응은 `이동량 = 풀 × min(1, 기본 속도 × 계수들의 곱)` 형태예요. 한 틱에 풀을 넘게 옮기는 일이 구조적으로 불가능해요.
- 셀끼리는 서로 영향을 주지 않아요(수평 이동 없음). 그래서 셀 순서와 무관하게 결과가 같지만, 결정론을 위해 행 우선 순서로 고정해요.

## 구간 선형 곡선

환경 계수는 데이터 파일의 `[[x, y], ...]` 곡선으로 계산해요.

- x는 엄격하게 오름차순이고, 점은 2개 이상이에요. y는 0 이상이에요.
- 범위 밖 x는 양 끝 값으로 고정해요(외삽하지 않아요).
- 코어에 `PiecewiseLinear` 타입 하나를 두고 모든 프로세스가 같이 써요.

## 명령 (M1)

| 명령 | 필드 | 효과 |
| --- | --- | --- |
| `Irrigate` | 대상 영역, `amount_mm` | `water_mm`에 더함 |
| `AddCompost` | 대상 영역, `compost_id`, `kg_per_m2` | 퇴비 정의에 따라 이분해성·난분해성 풀에 C, N을 더함 |
| `AddFertilizer` | 대상 영역, `fertilizer_id`, `g_per_m2` | 비료 정의에 따라 NH4+, NO3- 풀에 N을 더함 |
| `Noop` | - | M0 그대로 |

- **대상 영역**은 `TileArea{row, col, rows, cols}`예요. 그리드를 벗어나면 투입 단계에서 거부해요.
- `enqueue`는 `std::expected<std::uint64_t, Error>`를 돌려줘요. 모르는 퇴비·비료 ID, 음수 양, 영역 오류는 **투입 단계에서** 거부하고 큐에 넣지 않아요. (core-api 명세의 `enqueue` 반환형을 이렇게 바꿔요.)
- 기존 `variant` 순서는 유지하고 뒤에 추가해요: `Noop, Irrigate, AddCompost, AddFertilizer`.

## 장부 (보존 법칙 검증용)

월드는 누적 장부를 동적 상태로 가지고 있어요(직렬화·해시 대상).

| 항목 | 의미 |
| --- | --- |
| `water_in_mm`, `water_evaporated_mm`, `water_drained_mm` | 셀 합계, 면적 1 m² 기준 |
| `N_in_g`, `N_leached_g`, `N_gas_g` | 질소 투입·용탈·기체 손실 (M1에서 기체 손실은 0) |
| `C_in_g`, `C_respired_g` | 탄소 투입·호흡 손실 |

보존식 (셀 합계):

- 물: `현재 수분 = 초기 + water_in − evaporated − drained`
- 질소: `현재 (유기 N + NH4 + NO2 + NO3) = 초기 + N_in − N_leached − N_gas` (작물 흡수는 M2에서 항 추가)
- 탄소: `현재 유기 C = 초기 + C_in − C_respired`

테스트는 상대 오차 1e-9 이내로 검사해요(부동소수점 누적 오차만 허용).

## 데이터 배치

### `data/params.toml` (schema_version 2)

| 테이블 | 내용 | 명세 |
| --- | --- | --- |
| `[house]` | 하우스 기온 계수 | soil-temperature |
| `[soil_water]` | 증발, 배수 계수 | soil-water |
| `[soil_temperature]` | 완화 속도, 발열 계수 | soil-temperature |
| `[organic_matter]` | 분해 속도, 부식화, 곡선 | organic-matter |
| `[nitrification]` | 1·2단계 속도, pH·온도·수분·유리 암모니아 곡선, 아질산 임계치 | nitrogen |
| `[leaching]` | NH4+ 용액 비율 | nitrogen |
| `[[compost]]` | 퇴비 종류 정의 | organic-matter |
| `[[fertilizer]]` | 비료 종류 정의 | nitrogen |

### 시나리오 TOML (schema_version 2)

```toml
schema_version = 2

[scenario]   # M0과 같음 (name, seed, start_day_of_year, weather_csv)

[house]      # M0과 같음 (bed_rows, bed_cols)

[soil]       # 새로 추가: 모든 셀의 초기값 (M1은 균일 토양)
field_capacity_mm = 60.0
water_mm = 45.0
soil_temp_degC = 20.0
ph = 6.5
nh4_N_g_per_m2 = 2.0
no2_N_g_per_m2 = 0.0
no3_N_g_per_m2 = 10.0
labile_C_g_per_m2 = 50.0
labile_N_g_per_m2 = 5.0
stable_C_g_per_m2 = 3600.0
stable_N_g_per_m2 = 360.0

[[schedule]] # 새로 추가: 명령 목록 (선택, 0개 이상)
tick = 0
action = "add_compost"          # irrigate | add_compost | add_fertilizer
compost = "immature_manure"     # action에 따라 필요한 키가 달라요
kg_per_m2 = 3.0
# area = [row, col, rows, cols]  # 생략하면 하우스 전체
# repeat_every_ticks = 24        # 반복 (둘 다 쓰거나 둘 다 생략)
# repeat_count = 30
```

- `[[schedule]]`은 "같은 시드 + 같은 명령 목록" 재현을 데이터로 표현한 거예요. 로더가 이것을 `(tick, Command)` 목록으로 펼치고, CLI와 테스트가 해당 틱 직전에 `enqueue`해요. 코어는 일정표를 모르고 명령만 받아요.
- 모르는 `action`, action에 맞지 않는 키는 오류예요(엄격한 로딩).

## 스냅샷과 CLI 출력

- `Snapshot`에 `house_air_temp_degC`, 셀별 토양 상태 배열(행 우선), 장부를 추가해요.
- CLI CSV에는 셀 평균 토양 상태와 장부 열을 추가해요. 열 이름은 상태 변수 이름과 같아요.
- `tools/run_scenarios.py`: S1~S3 비교 쌍을 CLI로 돌리고, 같은 축에 겹친 그래프를 `runs/m1/`에 저장해요.
- `tools/plot_csv.py`: 여러 CSV를 받아 겹쳐 그리는 기능을 추가해요.

## 시나리오 테스트 (M1)

| ID | 시나리오 파일 (`data/scenarios/m1/`) | 기대 방향 | 판정 |
| --- | --- | --- | --- |
| S1 | `s1_immature.toml` 대 `s1_mature.toml`: 같은 양(3 kg/m²)의 미숙·숙성 퇴비 | 투입 후 7일 안에 토양 온도 최대 상승폭과 NH4+ 최대치가 미숙 쪽이 더 크다. 미숙 쪽은 기준(무처리)보다 토양 온도가 뚜렷이 높다 | 최대값 비교 |
| S2 | `s2_alkaline.toml`(pH 8.0) 대 `s2_neutral.toml`(pH 7.0): 황산암모늄 50 g/m² (10.5 g N/m²) | 21일 동안 NO2- 최대치가 알칼리 쪽이 더 크다. 아질산 독성 이벤트는 알칼리 쪽에서만 발생한다 | 최대값 비교, 이벤트 유무 |
| S3 | `s3_over.toml` 대 `s3_proper.toml`: 같은 질산태 비료 후 하루 30 mm 대 3 mm 관수 | 14일 뒤 NO3- 잔량이 과관수 쪽이 더 적고, 누적 용탈량은 더 크다 | 종료값 비교 |
| 보존 | 위 시나리오 전부 + 극한 입력 | 물·질소·탄소 보존식 성립, 모든 풀 0 이상, NaN 없음 | 매 틱 검사 |

"기준(무처리)" 비교용으로 `s0_control.toml`(명령 없음)도 둬요.

## 구현 작업 분할 (승인 후)

큰틀(패키지 0)은 메인 에이전트가 직접 하고, 나머지는 서브에이전트가 병렬로 맡아요. 패키지끼리는 파일이 겹치지 않아요.

| 패키지 | 내용 | 파일 | 담당 모델 |
| --- | --- | --- | --- |
| 0. 골격 | 상태·파라미터 구조체, `PiecewiseLinear`, 명령 타입과 검증, 틱 순서 배선(프로세스 함수는 빈 껍데기), 장부, 직렬화, 스냅샷 확장 | `sim-core/include/**`, `world.cpp`, `serialization.cpp` | 메인 (직접) |
| 1. 수분 + 용탈 | soil-water 명세, nitrogen 명세의 용탈 부분 | `sim-core/src/processes/soil_water.cpp`, `leaching.cpp` + 단위 테스트 | Sonnet |
| 2. 온도 | 하우스 기온, 토양 온도 완화 | `processes/temperature.cpp` + 단위 테스트 | Sonnet |
| 3. 유기물 분해 | 분해, 부식화, 부동화 제한, 발열 | `processes/organic_matter.cpp` + 단위 테스트 | **Opus** (질소 풀과 얽힘, 부동화 제한 로직) |
| 4. 질산화 | 2단계 질산화, 유리 암모니아 억제, 아질산 이벤트 | `processes/nitrification.cpp` + 단위 테스트 | **Opus** (계수 상호작용) |
| 5. 데이터·도구 | 로더 schema 2(파라미터, `[soil]`, `[[schedule]]`), CLI 열 추가, 그래프 도구 | `sim-data/**`, `sim-cli/**`, `tools/**` | Sonnet |
| 6. 통합 검증 | S0~S3 시나리오 파일, 시나리오·보존·극한 테스트, 그래프 생성 | `data/scenarios/m1/**`, `tests/test_m1_*.cpp` | Sonnet (1~5 완료 후) |

동시 실행은 최대 5개예요. 1~5를 함께 돌리고, 6은 그 결과를 합친 뒤에 실행해요.
