# M2 개요: pH·EC·통기 + 단순 작물

- 상태: 승인 (2026-11-18)
- M3 반영: 2026-11-18 ([m3-overview](m3-overview.md))
- M4 반영: 2026-11-18 ([m4-overview](m4-overview.md))
- 마일스톤: M2
- 관련 명세: [aeration](aeration.md), [soil-chemistry](soil-chemistry.md), [strawberry-minimal](strawberry-minimal.md), M1 명세 전체, [ADR-0007](../decisions/0007-acid-toxicity-and-manganese.md)
- 완료 기준(계획서 7장): S4 통과("비료를 줬는데 결핍" 재현), 과습 시 뿌리 손상 재현

M2는 처음으로 **작물**이 들어오는 마일스톤이에요. 토양에서 일어나는 일이 "작물이 실제로 얼마나 먹고 자라는가"로 이어져요.

## 플레이어 판단

| 판단 | 인과관계 | 시나리오 |
| --- | --- | --- |
| pH 관리 (석회, 비료 형태) | pH가 양분 **유효도**를 바꿔요. 총량이 충분해도 결핍이 생겨요 | S4 |
| 석회 시기 | 석회는 며칠~몇 주에 걸쳐 녹아요. 급하게 넣어도 바로 안 바뀌어요 | S9 |
| 시비량과 수분 | 많이 주면 EC가 올라 삼투 스트레스. 흙이 마르면 같은 양에서도 EC가 올라가요 | S10 |
| 관수와 경운 | 과습·다짐 → 통기 부족 → 뿌리 손상, 2단계 질산화 억제(아질산 축적), 분해 둔화 | S8 |
| 산성화 누적 | 질산화와 암모늄 흡수는 산을 만들어요. 강산성은 뿌리를 직접 상하게 해요(산성 장해) | S4b |
| 아질산과 pH | 같은 아질산 농도라도 산성 토양에서 독성이 커요 | 단위 테스트 |

## M2에서 다루지 않는 것

| 요소 | M2 처리 | 도입 |
| --- | --- | --- |
| 양이온 교환(CEC), 길항 | K·Ca·Mg는 M1의 NH4+처럼 **고정된 용액 비율**로만 표현. 완충력은 기본 완충력 + 유기물 기여분 | M3 |
| 인산 고정(시간에 따른 불용화) | 인산은 pH 유효도 곡선으로만 | M3 |
| 망간 | 없음 | M3 (ADR-0007) |
| 생육 단계, 화방, 과실, 품질 | 영양생장 한 단계만. 잎·뿌리 생체량만. (M4, 2026-11-18) M4에서 도입됐어요([strawberry-phenology](strawberry-phenology.md), [strawberry-fruit](strawberry-fruit.md)) | M4 |
| 증상 표현(황화, 팁번 등) | 충족도·스트레스 기록까지만 계산, 화면 증상 매핑은 안 함 | M4~M7 |
| 미생물, 병해 | 계수 1 | M5 |
| 환기·밀폐, 가스 장해 | 하우스 기온은 M1 식 그대로. (M4, 2026-11-18) 피복·측창·난방은 M4 [house-climate](house-climate.md)로 대체됐어요. 가스 장해는 M7 | M4(환기·난방), M7(가스) |
| CO2 | 계수 1. (M4, 2026-11-18) 하우스 CO2는 M4에서 도입됐어요([house-climate](house-climate.md)). CO2 시용기는 M7 | M4, M7 |

## 단위 규약 (M1에 추가)

| 양 | 단위 | 예 |
| --- | --- | --- |
| 작물 생체량 | g 건물/m² | `leaf_g_per_m2` |
| 양분(K, Ca, Mg, P, S) | 원소 기준 g/m² | `k_g_per_m2`, `p_g_per_m2` |
| 산·염기 | 당량(eq)/m² | `lime_undissolved_eq_per_m2` |
| 완충력 | eq/m² per pH | `base_buffer_eq_per_m2_per_pH` |
| EC | dS/m | `ec_dS_per_m` |
| 지수 | 0~1 무차원 | `aeration_index`, `compaction_index`, `root_health` |

## 상태 추가

### 토양 셀

| 이름 | 의미 | 명세 |
| --- | --- | --- |
| `saturation_mm` (정적) | 포화 수분량 | aeration |
| `compaction_index` | 다짐 지수 0~1 | aeration |
| `aeration_index` | 통기 지수 0~1 (매 틱 계산, 스냅샷 노출) | aeration |
| `k_g_per_m2`, `ca_g_per_m2`, `mg_g_per_m2`, `p_g_per_m2`, `s_g_per_m2` | 식물이 쓸 수 있는 양분 풀 (흙에 붙은 것 + 녹은 것) | soil-chemistry |
| `lime_undissolved_eq_per_m2`, `lime_ca_fraction`, `lime_mg_fraction` | 아직 녹지 않은 석회 | soil-chemistry |
| `ph` | **M2부터 변수** | soil-chemistry |
| `ec_dS_per_m` | 파생값 (매 틱 계산) | soil-chemistry |

### 작물 (타일당 0~1개)

잎·뿌리 건물량, 뿌리 건강도, 체내 양분(N, P, K, Ca, Mg), 생존 여부, 스트레스 기록. [strawberry-minimal](strawberry-minimal.md) (M4, 2026-11-18: 관부·개별 과실·화방·활착이 추가돼요, [m4-overview](m4-overview.md) "상태 추가")

### 장부 추가

(2026-11-18) 뿌리 고사로 토양에 들어간 탄소 `C_root_litter_g`도 추가해요. 토양 C 보존식: `유기 C = 초기 + C_in + C_root_litter − C_respired`. (M4, 2026-11-18) 낙엽·낙과·언 과실로 들어온 탄소 `C_leaf_litter_g`가 더해져 `유기 C = 초기 + C_in + C_root_litter + C_leaf_litter − C_respired`예요.

`water_transpired_mm`, 원소별 `*_in_g`, `*_leached_g`, `*_uptake_g` (N, P, K, Ca, Mg, S), `H_eq_net` (산 생성 누계, 검증용).

보존식은 "토양 + 작물 체내"를 하나의 계로 봐요. 흡수는 계 안의 이동이라 총량에서 빠지지 않아요. 토양만 따로 볼 때는 `− *_uptake_g` 항이 들어가요.

(M4, 2026-11-18) 수확 반출 장부 `N_harvested_g`, `P_…`, `K_…`, `Ca_…`, `Mg_…`, `Mn_harvested_g`가 추가돼 계 전체 보존식은 `토양 + 체내 = 초기 + in − leached − gas − harvested`예요. `N_gas_g`는 탈질로 0이 아니게 돼요([m4-overview](m4-overview.md) "장부 추가").

## 틱 처리 순서 (M2)

(M4, 2026-11-18) 아래 순서는 M4에서 f1(탈질), h0~h6(생물계절·과실), 하우스 CO2 갱신 등이 끼어들어 [m4-overview](m4-overview.md) "틱 처리 순서 (M4)"가 대체해요.

```
매 틱마다:
  1. 명령 적용                    (+ 석회, 경운)
  2. 하우스 환경
  3. 각 토양 셀 (행 우선):
     a. 수분: 증발(작물 그늘만큼 감소) → 증산 → 배수(다짐에 따라 느려짐, 포화 초과분은 즉시)
     b. 온도
     c. 통기: aeration_index 계산                         ← M2
     d. (미생물: M5)
     e. 유기물 분해 (산소 계수 적용)                      ← M2 변경
     f. 질산화 (1·2단계 산소 계수, 생성된 H+ 기록)        ← M2 변경
     g. 석회 용해 → pH 갱신                                ← M2
     h. 작물: 스트레스 계산 → 생장 → 흡수(H+/OH- 교환 기록) → 뿌리 건강도 ← M2
     i. 용탈 (K, Ca, Mg, P, S 포함)                        ← M2 확장
     j. 파생값: EC                                         ← M2
     k. 이벤트 판정: 아질산(pH 독성 배율 적용), 작물 고사  ← M2 변경
```

- g의 pH 갱신에는 f(질산화), 1(비료·석회의 직접 효과)에서 이번 틱 생긴 H+가 들어가요. h(흡수)에서 생긴 H+/OH-는 **다음 틱** g에 반영해요. (같은 틱 안에서 흡수가 pH 갱신보다 뒤라서요.)
- 기존 원칙 그대로: 이동량은 원천 풀로 제한, 모든 1차 반응은 `풀 × min(1, 속도 × 계수)`.

## 명령 추가

| 명령 | 필드 | 효과 |
| --- | --- | --- |
| `AddLime` | 영역, `lime_id`, `g_per_m2` | 미용해 석회 풀에 당량을 더함 (soil-chemistry) |
| `Till` | 영역 | 다짐 지수를 `tilled_compaction_index`로. 작물이 있으면 뿌리 손상 (aeration) |

`variant` 순서: `Noop, Irrigate, AddCompost, AddFertilizer, AddLime, Till`. (M4, 2026-11-18: 뒤에 `SetWindow, SetHeater, SetCoverLayers, ThinFlowers`가 추가돼요, [m4-overview](m4-overview.md) "명령")

## M1 명세 변경 (M2 승인 시 해당 문서에 반영)

| 문서 | 변경 |
| --- | --- |
| soil-water | 포화 수분량 상한(초과분 즉시 배수), 배수 속도에 다짐 곡선 곱함, 작물 증산 항, 토양 증발에 `(1 − 수관 차광)` 곱함 |
| organic-matter | `O2계수 = decomposition_o2_curve(aeration_index)` |
| nitrogen | 1·2단계 산소 곡선(2단계가 더 민감), 질산화 1 mol N당 H+ 2 eq 생성 기록, 아질산 판정은 `NO2 × nitrite_toxicity_ph_curve(pH)`(M2 계획서 반영 항목), 용탈 대상에 K·Ca·Mg·P·S 추가 |
| m1-overview | 장부·보존식 확장 |

## 데이터 배치

### `data/params.toml` (schema_version 3)

M1 테이블에 추가: `[aeration]`, `[soil_chemistry]`, `[ions]`(원소별 당량 무게), `[availability]`(원소별 pH 유효도 곡선), `[root_damage]`, `[crop]`(딸기 계수), `[[lime]]`. `[[fertilizer]]`와 `[[compost]]`에 K·Ca·Mg·P·S 함량과 `direct_acid_eq_per_kg` 추가.

### 시나리오 TOML (schema_version 3)

```toml
[soil]       # M1 항목 + 아래 (M2 시나리오의 water_mm 기본은 50.0)
saturation_mm = 110.0
compaction_index = 0.3
base_buffer_eq_per_m2_per_pH = 5.0
k_g_per_m2 = 60.0
ca_g_per_m2 = 400.0
mg_g_per_m2 = 60.0
p_g_per_m2 = 25.0
s_g_per_m2 = 3.0

[crop]       # 생략하면 작물 없음
leaf_g_per_m2 = 30.0     # (M4, 2026-11-18 삭제. 현재는 아래 "시나리오 이행 (M4)")
root_g_per_m2 = 10.0     # (M4, 2026-11-18 삭제)

[difficulty]
stress_combination = "minimum"   # minimum | product
```

## 시나리오 테스트 (M2)

| ID | 비교 | 기대 방향 | 기간 |
| --- | --- | --- | --- |
| **S4** | `s4_optimal`(pH 6.2) 대 `s4_alkaline`(pH 8.0), 같은 토양 P 총량 | 알칼리 쪽 P 충족도가 낮다. **토양 P 총량(유효 + 고정)은 알칼리 쪽이 같거나 더 많다** (M3, 2026-11-18 디렉터 결정 2번: 인산 고정이 생겨 유효 P 풀 대신 총량으로 비교. 원래 뜻인 "총량이 충분해도"를 유지) | 21일 |
| S4b | `s4_optimal` 대 `s4_acid`(pH 4.6) | 산성 쪽 뿌리 건강도와 생장량이 낮다 (산성 장해). Ca·Mg 결핍은 M3 양이온 교환 이후에 다시 정의 | 21일 |
| **S8** | `s8_proper`(경운, 하루 6 mm) 대 `s8_waterlogged`(다짐 0.8, 하루 25 mm) | 과습 쪽 통기 지수 최저값·뿌리 건강도·생장량이 낮다 | 14일 |
| S9 | 산성 토양(pH 5.0)에 석회 투입 대 무처리 | pH가 **서서히** 오른다: 1일 뒤 상승폭 < 14일 뒤 상승폭. 무처리는 오르지 않는다 | 14일 |
| S10 | 표준 시비 대 과다 시비(N·K 5배) | 과다 쪽 EC가 높고 생장량이 낮다. 같은 시비에서 물을 끊으면 EC가 더 오른다 | 14일 |
| 보존 | 위 전부 + 극한 | 물·N·P·K·Ca·Mg·S·C 보존(토양 + 작물), 모든 풀 ≥ 0, pH는 0~14, NaN 없음 | 매 틱 |

작물이 있는 시나리오는 별도 표기가 없으면 매일 8시에 6 mm 관수해요(물 스트레스가 다른 효과를 가리지 않도록).

### 시나리오 이행 (M4, 2026-11-18)

M2 시나리오는 모두 schema 5로 올라가요([m4-overview](m4-overview.md) "시나리오 이행"). 하우스 기후와 작물 초기값이 바뀌어서 다음이 들어가요.

- `[house] cover_layers = 3`.
- **매일 10~16시에 창 6개 전부를 열고**(`SetWindow`, `open_fraction = 1.0`) 16시에 닫는 일정. 닫힌 3중은 M1 식과 같지 않아서 환기가 없으면 다른 값을 내요([house-climate](house-climate.md)).
- 작물이 있는 시나리오(S4·S4b·S8·S10 등)는 `[crop]`을 `leaf_g_per_m2 = 30`, `root_g_per_m2 = 10` 대신 `seedling_grade = "B"`, `plants_per_m2 = 8`(잎 28, 관부 16, 뿌리 12 g/m²), **`initial_establishment_index = 1.0`**(이미 자란 작물)으로 바꿔요.
- 호흡·분배·활착이 바뀌어 생장량 수치는 달라지지만, S4·S4b·S8·S9·S10의 방향 테스트는 그대로 통과해야 해요. 기대값이 바뀌는 단위 테스트는 m4-overview "기대값이 바뀌는 단위 테스트"의 승인 목록을 따라요.

S7(무처리 대조구에서 양분 고갈)은 날씨에 따른 생장 감소와 구분하기 어려워서 생육 단계가 생기는 M4로 미뤄요. (M4, 2026-11-18) M4의 S7로 다뤄요([m4-overview](m4-overview.md) "시나리오 테스트").

## 구현 작업 분할 (승인 후 확정)

| 패키지 | 내용 | 파일 | 담당 모델 |
| --- | --- | --- | --- |
| 0. 골격 | 상태·파라미터·명령·작물 구조체, 프로세스 선언과 빈 껍데기, 틱 배선, 테스트 도우미 | 헤더, `world.cpp` 배선 | 메인 (직접) |
| 0b. 검증·직렬화 | 파라미터·시나리오·명령 검증, 세이브 형식 v3 | `validation.cpp`, `serialization.cpp` | Sonnet |
| 1. 통기·수분·기존 프로세스 변경 | aeration, soil-water 변경분, 경운, 분해·질산화 산소 계수, 질산화 H+, 아질산 pH 독성 배율 | `aeration.cpp`, `soil_water.cpp`, `organic_matter.cpp`, `nitrification.cpp` | Sonnet |
| 2. 토양 화학 | 비료·퇴비 무기 성분, 석회 용해, pH 수지, 유효량, EC, 용탈 확장 | `soil_chemistry.cpp`, `leaching.cpp` | **Opus** (산·염기 수지와 여러 풀이 얽힘) |
| 3. 작물 | 생장, 흡수, 증산, 뿌리 건강도, 스트레스 기록 | `crop.cpp` | **Opus** (스트레스 결합, 흡수 제한 로직) |
| 5. 데이터·도구 | schema 3 로더, params·시나리오, CLI 열, 그래프 그룹 | `sim-data/**`, `sim-cli/**`, `tools/**`, `data/` | Sonnet |
| 6. 통합 검증 | S4·S4b·S8·S9·S10 시나리오, 보존·극한·결정론 테스트, 그래프 | `data/scenarios/m2/**`, `tests/test_m2_*.cpp` | Sonnet (나머지 완료 후) |

동시 실행은 최대 5개예요 (0b, 1, 2, 3, 5를 함께, 6은 그 뒤).
