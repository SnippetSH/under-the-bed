# M4 개요: 딸기 완성 (생육 단계, 화방, 과실, 품질, 탈질, 모종 등급)

- 상태: 승인 (2026-11-18, 프로토타입 점검 결과 반영본 최종 승인)
- 마일스톤: M4
- 관련 명세: [strawberry-phenology](strawberry-phenology.md), [strawberry-fruit](strawberry-fruit.md), [denitrification](denitrification.md), [house-climate](house-climate.md), M1~M3 명세 전체, [ADR-0006](../decisions/0006-off-season-and-disinfection.md), [ADR-0011](../decisions/0011-house-climate-in-m4.md)
- 완료 기준(계획서 7장): 헤드리스로 1작기 완주, 처방별 수확량·당도 차이가 그래프로 확인됨

M3까지는 "토양이 작물의 잎·뿌리를 얼마나 키우는가"였어요. M4는 **"그래서 언제, 얼마나, 어떤 딸기를 따는가"**예요. 처음으로 정식부터 수확 종료까지 한 작기가 돌아가요.

## 플레이어 판단

| 판단 | 인과관계 | 시나리오 |
| --- | --- | --- |
| 웃거름을 줄까 말까 | 안 주면 후반(봄)에 양분이 떨어져 수확이 줄어요 | S7 |
| 질소를 얼마나 | 많이 주면 화아분화가 늦어 첫 수확이 밀리고, 덜 달고 물러요 | S14 |
| 어떤 모종을 살까 | 좋은 모종은 첫 수확이 빨라요(가격은 M7) | S15 |
| 칼륨·칼슘 관리 | K 부족은 당도, K 과다(→ Ca 길항)는 경도를 떨어뜨려요 | S16, S17 |
| 물 관리 | 과습은 용탈에 더해 탈질로도 질소를 잃게 해요 | S13 |
| 과실 부담 | 겨울 약광에 과실이 많이 달리면 과실이 작아지고 포기가 지쳐요 | 단위 테스트 |
| 하우스 기후 관리 | 창을 안 열면 가을에 더워 화아분화가 막히고, 난방을 안 하면 겨울에 잎·과실이 얼어요. 피복을 늘리면 따뜻하지만 어두워요. 창을 닫으면 따뜻하지만 낮에 CO2가 떨어져 광합성이 줄어요 | S18 |

## M4에서 다루지 않는 것

| 요소 | M4 처리 | 도입 |
| --- | --- | --- |
| 수확·적엽 명령, 과숙 손실 | 익으면 자동 수확, 늙은 잎은 자동 낙엽 (적화·적과는 M4 명령) | 이후 |
| 난방 연료비, 피복 추가금, 자동 개폐기·온도 연동 장비 | 에너지 장부만 기록 (명령은 M4, [house-climate](house-climate.md)) | M7 |
| 습도, 결로, 보온 커튼 개폐 | 없음 | M5·M8 |
| 판매 등급·가격·저장 | 품질 지표(생체중, 당도, 경도, 기형과)까지만 | M7 |
| 증상 표현(팁번, 황화, 망간 반점 등) | 충족도·길항 배율·stress_hours·품질 지표까지만 | M7 |
| 휴면, 런너, 육묘 | 없음 | v1 비목표 (ADR-0006) |
| 미생물·병해, 소독 | 계수 1 | M5 |
| 붕소(기형과) | 없음 | M8 |
| CO2 시용기 | 없음 (하우스 CO2 수지는 M4) | M7 |
| 하우스 안 바람 세기(공기 흐름) | 없음. 환기는 열·CO2 교환으로만 | 습도·병해 M5, 증산·Ca 이동 M7 |

## 단위 규약 (M3에 추가)

| 양 | 단위 | 예 |
| --- | --- | --- |
| 과실 수 | 개/m² | `fruits_per_m2` |
| 과실 1개 건물·생체중 | g | `dry_g_per_fruit` |
| 수확 생체중 | g/m² | `harvested_fresh_g_per_m2` |
| 당도 | °Brix | `brix` |
| 적산온도 | °C·일 | `degree_days_to_harvest` |
| 낮 길이 | h | `day_length_h` |

## 상태 추가

| 위치 | 이름 | 명세 |
| --- | --- | --- |
| 하우스 | `house_air_temp_mean_degC` | strawberry-phenology |
| 작물 | `plants_per_m2`, `establishment_index`, `induction_index`, `trusses` | strawberry-phenology |
| 작물 | `crown_g_per_m2`, `fruits`(개별 과실), 수확 기록(크기 구간 포함), `size_satisfaction_mean`(크기 충족도 이동 평균, 저장 상태) | strawberry-fruit |
| 작물 | 진단·리포트용 기록(아래 "진단·리포트용 기록"). 시뮬레이션 결과에는 영향이 없어요 | - |

### 장부 추가

`N_harvested_g`, `P_…`, `K_…`, `Ca_…`, `Mg_…`, `Mn_harvested_g`, `C_leaf_litter_g`. `N_gas_g`가 탈질로 처음 0이 아니게 돼요.

보존식: 원소별 `토양 + 체내 = 초기 + in − leached − gas − harvested`. 토양 C: `유기 C = 초기 + C_in + C_root_litter + C_leaf_litter − C_respired`.

## 하우스 기후 관리 (디렉터 결정 1번, [house-climate](house-climate.md), [ADR-0011](../decisions/0011-house-climate-in-m4.md))

난방·환기는 **플레이어가 직접** 해요. 안 하면 겨울 밤에 0 °C 근처나 아래로, 9월 한낮에 30 °C 위로 가요. 하우스는 **기본 3중 피복**이고 겹마다 양쪽 측창이 있어 **창 6개**를 열고 닫아요. 휴작기(4~8월 무렵)에 2중·4중으로 바꿀 수 있고(추가금은 M7), 피복이 많을수록 따뜻하지만 어두워요. 저온에서는 잎과 과실이 얼어요. (프로토타입 점검 후) 일사 이득을 올려서 닫힌 3중 하우스는 1월 맑은 한낮에도 약 20~25 °C가 돼요. 대신 M1 식과 더는 같지 않아서, M1~M3 시나리오에는 매일 한낮 환기 일정이 들어가요([ADR-0011](../decisions/0011-house-climate-in-m4.md) "점검 후 보완").

## 틱 처리 순서 (M4)

```
매 틱마다:
  1. 명령 적용
  2. 하우스 환경: 피복·창 → 평형 기온·난방 → 기온(열 관성) → 이동 평균, 바깥 CO2(시각 표) ← M4
  3. 각 토양 셀 (행 우선):
     a. 수분  b. 온도  c. 통기  d. (미생물: M5)
     e. 유기물 분해
     f. 질산화
     f1. 탈질                                                              ← M4
     f2. 망간 산화·환원 → 양이온 교환
     f3. 인산 고정
     g. 석회 용해 → pH (탈질의 H+ 소비 포함)                              ← M4 변경
     h. 작물:
        h0. 저온 피해 (잎·과실·꽃)                                         ← M4
        h1. 생물계절: 활착 → 화아분화 → 화방 발달·개화(꽃 생성) → 과실별 개화 판정 ← M4
        h2. 스트레스
        h3. 총생산 − 유지 호흡 → 분배(과실 따라잡기 수요 우선 → 잎·관부·뿌리)  ← M4
        h4. 크기 충족도 이동 평균 → 과실 진행·품질 적산 → 자동 수확(양분 반출) → 낙과  ← M4
        h5. 흡수 (요구량에 관부·과실 포함, 활착 배율, N 전용 속도)          ← M4 변경
        h6. 뿌리 건강도·고사 → 낙엽                                        ← M4
     i. 용탈  j. 파생값: EC  k. 이벤트 판정
  4. 하우스 CO2 갱신 (이번 틱 작물 흡수·토양 호흡·환기)                 ← M4
```

## 명령

`SetWindow{layer, side, open}`, `SetHeater{on, setpoint_degC}`, `SetCoverLayers{layers}`(휴작기만)([house-climate](house-climate.md)), `ThinFlowers{area, per_plant}`(적화·적과, [strawberry-fruit](strawberry-fruit.md) 6c, `per_plant`는 포기당 딸 개수)를 추가해요. `variant` 순서: `Noop, Irrigate, AddCompost, AddFertilizer, AddLime, Till, SetWindow, SetHeater, SetCoverLayers, ThinFlowers`. 정식은 지금처럼 시나리오 시작 시점이에요.

## M1~M3 명세 변경 (M4 승인 시 해당 문서에 반영)

| 문서 | 변경 |
| --- | --- |
| strawberry-minimal | 생장 = 총생산 × 활착 배율 − 유지 호흡(순생장이 음수일 수 있음), 분배는 strawberry-fruit, `leaf_allocation_fraction` 삭제, 요구량에 관부·과실, 뿌리 고사의 비율 분모에 관부·과실 포함, **N 전용 사치 상한(`n_luxury_uptake_ratio` 1.2)·흡수 속도(`n_uptake_rate_per_g_root_per_hour` 0.00006), 모든 원소 흡수 능력에 활착 배율**, 단순화의 "생육 단계·호흡·노화 (M4)" 정리. 시나리오 `[crop]`은 모종 등급·재식 밀도·초기 활착 지수 |
| soil-temperature | 하우스 기온 식을 [house-climate](house-climate.md)의 다중 피복 열수지로 대체, M1 `[house]` 계수 3개 삭제 |
| soil-water | 하우스 안 일사 = 일사 × 투과율^겹수 (`solar_transmittance_factor` 대체) |
| nitrogen | 탈질(f1) 추가, 기체 손실이 0이 아님. 단순화의 "탈질 없음 (M4)" 정리 |
| soil-chemistry | pH 수지에 탈질의 H+ 소비 |
| organic-matter | 낙엽 C·N이 이분해성 풀로 (뿌리 고사와 같은 규칙) |
| m1-overview, m2-overview, m3-overview | 장부·보존식에 `harvested`, `C_leaf_litter` |

## 데이터 배치

### `data/params.toml` (schema_version 5)

추가: `[phenology]`, `[fruit]`, `[denitrification]`, `[calendar] day_length_curve`, `[house]` 재구성(house-climate, 난방 설정 범위 `heater_setpoint_min_degC`·`heater_setpoint_max_degC` 포함), `[crop]`의 분배·호흡·낙엽·과실 적정 농도·저온 피해 키·N 전용 흡수 키(`n_luxury_uptake_ratio`, `n_uptake_rate_per_g_root_per_hour`), `[[seedling_grade]]`. `[fruit]`의 과위 배열 4개(`rank_fraction`, `rank_anthesis_delay_days`, `rank_potential_fresh_g`, `rank_abortion_weight`)는 **길이 N으로 모두 같아야 해요**(검증에서 확인, 현재 N = 5). `seedling_grade.initial_induction_index`의 검증 범위는 [0, 1]이에요. 삭제: `crop.leaf_allocation_fraction`, `house.base_offset_degC`, `house.solar_gain_degC_per_W_per_m2`, `house.solar_transmittance_factor`, `fruit.abortion_rate_per_hour`(→ `abortion_rate_per_development_index`), 초안의 단일 `fruit.brix_temp_curve`(→ 낮·밤 점수 곡선 두 개와 `brix_temp_score_curve`).

### 시나리오 TOML (schema_version 5)

```toml
[crop]                      # 생략하면 작물 없음
seedling_grade = "B"
plants_per_m2 = 8.0
initial_establishment_index = 0.0   # 필수 (0~1). 작기 시나리오 0, M2·M3 이행 시나리오·이미 자란 작물 테스트 1

[house]                     # M1 항목 + 아래
cover_layers = 3            # 2~4
```

**새 필수 키 `[crop] initial_establishment_index`**(0~1, 2026-11-18 프로토타입 점검으로 신설): 작물의 시작 활착 정도예요. 막 정식한 작기 시나리오는 0이고(정식 몸살부터 시작), "이미 자란 작물"을 가정하는 M2·M3 이행 시나리오와 단위 테스트는 1이에요. 생략하면 로더가 거부해요(기본값으로 숨기지 않아요).

### 시나리오 이행 (M1~M3)

모든 시나리오를 schema 5로 올려요. 작물이 있는 M2·M3 시나리오는 `leaf_g_per_m2 = 30`, `root_g_per_m2 = 10` 대신 `seedling_grade = "B"`, `plants_per_m2 = 8`(잎 28, 관부 16, 뿌리 12 g/m²)을 써요. 모든 시나리오에 `cover_layers = 3`을 넣어요. 작물이 있는 시나리오에는 `initial_establishment_index = 1`도 넣어요(이미 자란 작물). **매일 한낮 환기 일정**도 넣어요: 매일 10~16시에 창 6개 전부를 연다(`SetWindow`, `open_fraction = 1.0`, 16시에 0으로 닫음). 일사 이득을 올린 뒤 닫힌 3중은 M1 식과 더 이상 같지 않아서, 환기가 없으면 M1~M3 시나리오가 다른 값을 내요(2026-11-18 프로토타입 점검, [ADR-0011](../decisions/0011-house-climate-in-m4.md) "점검 후 보완"). 시나리오 TOML에서 창 명령의 필드 이름은 `open_fraction`(0~1)이에요. 호흡·분배·활착이 바뀌어 생장량 수치는 달라지지만, M1~M3의 방향 테스트는 그대로 통과해요. 수치 변화와 기대값이 바뀌는 단위 테스트 목록은 아래 "프로토타입 점검"에 적었어요.

## 시나리오 테스트 (M4)

**작기 시나리오**(`m4/season_*`): 9월 10일(day 253) 정식, 263일(5월 31일까지, 6,312틱), 모종 B 8포기/m²(`initial_establishment_index` 0), 기준 토양(M3), 3중 하우스, 시드 20261118. 값은 2026-11-18 프로토타입 점검에서 확정했어요(처방·기후 일정 전부 게임용 임의값).

**기준 처방(`season_standard`)**

| 항목 | 값 |
| --- | --- |
| 토양 | 기준 토양(M3)에서 NO3 4 g/m², 이분해성 유기 N(`labile_N`) 1 g/m², 난분해성 유기 N(`stable_N`) 200 g/m²로 (기준보다 질소를 적게 시작) |
| 기비 | 숙성 퇴비(`mature_manure`) 2 kg/m² (tick 0). 질산칼슘 기비는 없음 |
| 웃거름: 질산칼슘 | 2주(336틱)마다. 9/24부터 5 g/m² 3회, 11/5부터 **2.5 g/m²** 9회(11~2월은 절반, 겨울 N 줄이기), 3/11부터 5 g/m² 5회 (모두 합쳐 17회) |
| 웃거름: 황산칼륨 | 9/24부터 2주마다 6 g/m², 17회 |
| 관수 | 매일 08시: 9/10~10/31 4 mm, 11/1~2/28 2.5 mm, 3/1~5/31 4 mm |
| 난방 | 11/1~4/1 켜짐, 설정 10 °C (tick 1248 켜기, 4872 끄기) |

**기후 일정**(매일 반복, 모두 열었다가 정해진 시각에 닫음):

| 기간 | 열림 시각 | 열 창 | 개방률 |
| --- | --- | --- | --- |
| 9/10~10/31 | 8~18시 | 6개 전부(겹마다 양쪽) | 100% |
| 11/1~11/30 | 12~14시 | 3개(겹마다 한쪽) | 100% |
| 12/1~1/31 | 12~13시 | 3개 | 30% |
| 2/1~2/28 | 12~14시 | 3개 | 30% |
| 3/1~3/31 | 11~14시 | 3개 | 30% |
| 4/1~4/30 | 9~17시 | 3개 | 50% |
| 5/1~5/30 | 8~18시 | 6개 전부 | 100% |

(점검 전 초안은 "맑은 낮에 열고 저녁에 닫고, 11월부터 난방 6 °C"였어요. 2차 점검에서 월별 낮 환기로 다시 짰고, 6차에서 단계 개방(0·30·50·70·100%)이 들어왔고, 7차에서 11월 환기를 고쳤어요.)

**처방 변형**(`season_standard`에서 바꾼 것만):

| 시나리오 | 바꾼 것 |
| --- | --- |
| `season_no_topdress`(S7) | 웃거름 전부 없음 (퇴비 기비와 관수는 같음) |
| `season_n_excess`(S14) | 기비에 요소 30 g/m² 추가. 웃거름은 질산칼슘 5 g을 겨울 감량 없이 17회, 황산칼륨 6 g 17회는 그대로, **요소 3.37 g(N 1.55 g)을 같은 주기로 17회** 더해요. 한 회차 웃거름 N이 표준 5 g 회차(질산칼슘 5 g = N 0.775 g)의 3배(2.325 g)가 돼요 |
| `season_k_low`(S16) | 초기 K 10 g/m²(기준 60), 퇴비 기비 대신 질산칼슘 15 g + 과인산석회 30 g(K 없는 기비), 황산칼륨 웃거름 없음. 질산칼슘 웃거름은 표준과 같음 |
| `season_k_excess`(S17) | 황산칼륨 웃거름 18 g(3배), 나머지는 표준 |
| `season_acid` | 초기 pH 5.0, 나머지는 표준 |
| `season_thinned`(신설) | 표준 + 적화·적과 `ThinFlowers` 포기당 6.0개를 화방 개화 약 5일 뒤마다 7회(tick 1205, 1994, 2847, 3686, 4334, 4861, 5533). [strawberry-fruit](strawberry-fruit.md) 6c |

| ID | 비교 | 기대 방향 | 기간 |
| --- | --- | --- | --- |
| **1작기** | `season_standard` | 끝까지 돌고, **12월 15일 이전에 첫 수확**, 12월~5월 매달 수확이 있다. 작물이 살아 있다 (디렉터 결정 11번) | 263일 |
| **처방 비교** | `season_standard` 대 S7·S14·S16·S17 작기 시나리오, `season_acid`(pH 5.0), `season_thinned`(적과) | 누적 수확량과 평균 당도가 처방마다 다르고, 그래프로 보인다(완료 기준) | 263일 |
| S7 | `season_standard` 대 `season_no_topdress`(웃거름 없음) | 무처리 쪽 **3~5월 평균 N 충족도가 낮고** 누적 수확량이 적다 (K는 S16에서, 디렉터 결정 11번) | 263일 |
| S13 | `s13_proper` 대 `s13_waterlogged` ([denitrification](denitrification.md)) | 과습 쪽 `N_gas_g`가 크고, 적정 쪽은 0에 가깝다 | 14일 |
| S14 | `season_standard` 대 `season_n_excess`(웃거름 N 3배, 요소 추가) | 질소 과다 쪽 첫 화방 분화와 첫 수확이 늦고, 평균 당도가 낮다 | 263일 |
| S15 | `s15_grade_a` 대 `s15_grade_c` (기준 처방) | A 쪽 첫 수확이 빠르고, 12월 31일까지 누적 수확량이 많다 | 113일 |
| S16 | `season_standard` 대 `season_k_low`(초기 K 10, K 웃거름 없음) | K 부족 쪽 평균 당도가 낮다 | 263일 |
| S17 | `season_standard` 대 `season_k_excess`(황산칼륨 웃거름 3배) | K 과다 쪽 평균 경도가 낮다(Ca 길항) | 263일 |
| S18 | 겨울 무관리 대 난방, 가을 창 닫힘 대 환기, **CO2 닫힘 대 환기** ([house-climate](house-climate.md)) | 무관리 겨울: 최저 기온 < 0 °C, 잎 손실. 난방(6 °C): 최저 ≈ 설정, 에너지 > 0. 가을 닫힘: 최고 > 30 °C, 화아분화 느림. CO2 닫힘: 낮 최저 CO2가 낮고 총생산이 작다(에너지 조건은 뺌) | 겨울 7일, 가을 28일, CO2 7일 |
| 회귀 | M1 S1~S3, M2 S4·S4b·S8·S9·S10, M3 S5·S6·S11·S12 | 기존 방향 그대로 | 각 기간 |
| 보존 | 위 전부 + 극한(작기 중 관수 0, 질소 100배, 겨울 난방 없음, 4중 + 창 전부 열림 + 난방) | 물·N·P·K·Ca·Mg·Mn·S·C 보존(토양 + 작물 + 수확), 모든 풀 ≥ 0, NaN 없음 | 매 틱 |
| 성능 | `season_standard` 헤드리스 실행 | Release 빌드에서 몇 초 안 (계획서 9장 목표) | - |

- 측정 정의는 M3와 같아요(시간별 하우스 평균, 장부는 하우스 합계). "평균 당도·경도"는 작기 전체 수확의 생체중 가중 평균이에요. "첫 수확"은 하우스에서 처음 `FruitHarvested`가 나온 틱이에요.

## 디렉터 결정 (2026-11-18)

1. **하우스 기후는 플레이어가 직접 관리**: 기본 3중 피복, 겹마다 양쪽 측창(창 6개), 휴작기(4~8월 무렵)에 추가금으로 2중·4중 변경. 관리하지 않으면 겨울에 0 °C 근처나 아래, 가을에 30 °C 위로 가요. → [house-climate](house-climate.md), [ADR-0011](../decisions/0011-house-climate-in-m4.md)
2. **개별 과실**: 과실마다 과위·개화 시점·수정률·크기·품질을 들고, 수정률과 크기 변이는 시드 기반 생물 변이 난수로 정해요([strawberry-fruit](strawberry-fruit.md)).
3. **자동 수확**: M4는 익으면 바로 따요. 플레이어 수확·적엽·적과 명령은 이후 마일스톤에서.
4. **유지 호흡과 낙엽 도입** (권장안대로).
5. **시나리오 작물 초기값을 모종 등급으로** (권장안대로): M2·M3 시나리오도 `seedling_grade = "B"`, `plants_per_m2 = 8`.
6. **증상 표현은 M7** (권장안대로): M3의 팁번 포함. M4 품질 지표가 그 재료예요.
7. **하우스 CO2를 M4에**: 바깥 CO2는 디렉터 지정 하루 변동 식(새벽 약 500, 오후 약 390 ppm). 작물 흡수·토양 호흡·환기로 하우스 농도가 움직이고 총생산에 곱해요. CO2 시용기는 M7.
8. **바람 세기**는 M5(습도·병해), M7(증산·Ca 이동)에서 이어서 다뤄요.
9. **과실 수** (3차 점검 후): 꽃 수는 실제처럼 두고, 생장량이 모자라면 어린 과실이 떨어져요(낙과). 플레이어가 칸(8포기)마다 상태를 보고 포기당 몇 개를 딸지 정하는 적화·적과 명령 `ThinFlowers`를 M4에 넣어요.
10. **당도 온도**: 주·야간 이중 가중(낮은 23 °C 중심 가우시안을 일사 가중, 밤은 8.5 °C 중심 비대칭 곡선을 시간 가중, 비율 0.6 : 0.4).
11. **점검 마무리 결정** (4차 후): 수확량 약 5 kg/m² 유지(국내 촉성 3.5~5 t/10a 위쪽), 겨울 체내 N 상한 근처 수용, S7 조건은 "3~5월 N 충족도 낮고 수확량 적음"(K는 S16), 1작기 조건은 "12월 15일 이전 첫 수확, 12~5월 매달 수확, 생존", 점검으로 바뀐 값·구조 일괄 승인.
12. **낙과는 진행도 기준** (5차): 시간당 확률 대신 진행도 증가분에 비례. 겨울에 천천히 크는 과실이 더 많이 떨어지던 문제를 고쳐요. 광이용효율 10% 낮춤(2.5 → 2.25) 승인.
13. **수확 파동 완화** (5차 후): 과위를 5단계로(개화 지연 0·4·7·10·12일), 과실마다 개화 시점 생물 변이. 측창은 0·30·50·70·100% 단계 개방, 환기 경로는 직렬 개구부 식.

## 프로토타입 점검 (2026-11-18, 7차)

M3와 같은 방식이에요. 명세 승인 뒤 버리는 브랜치(`proto/m4-check`, 커밋 377ee1c)에서 7차에 걸쳐 돌려 초기값·처방·기후 일정을 맞췄어요. 아래 결과와 바뀐 값으로 최종 승인을 받아요. 최종 승인 뒤 그 프로토타입을 본 구현의 출발점으로 써요.

### 차수별 기록

| 차 | 결과 | 문제 → 결정 |
| --- | --- | --- |
| 1 | 수확 0 | 문제: 망간이 체내에 쌓임, 겨울 하우스가 너무 추움, 질소 비율이 화아분화 곡선을 포화시킴, 화방이 너무 느림, 밤에 과실 공급이 0이라 당도가 깎임. → 일사 이득 0.063 → 0.18, 화방 적산온도 600 → 350, N 전용 사치 상한·흡수 속도 신설, 당도 공급 입력을 크기 충족도로 (정확한 값은 아래 "초안 대비 바뀐 값") |
| 2 | 12~5월에 수확은 나오지만 과실이 작고 당도가 낮음 | 문제: 작기 내내 망간이 쌓여 독성(`mn_uptake_rate` 0.00004 → 0.00001). 작기 시나리오 기후를 월별 낮 환기로 다시 짬 |
| 3 | 당도 10, 과실 14 g, 수확량 5.4 kg/m² | 처방 확정: 기비 질산칼슘 제거, 웃거름 질산칼슘 5 g + 황산칼륨 6 g, 매일 08시 관수 |
| 4 | B 과위 낙과, 적화·적과, 주·야간 이중 당도를 도입. 1~2월에 수확이 비는 구간이 생김 | 디렉터 결정 9·10번(낙과, 적과 명령, 이중 가중 당도)과 11번(수확량 약 5 kg/m² 유지, 겨울 체내 N 상한 근처 수용, S7·1작기 조건, 바뀐 값·구조 일괄 승인) |
| 5 | 낙과를 진행도 기준으로 바꿈. 1~2월 공백은 같은 시기에 핀 과실이 좁은 수확 파동으로 함께 익어서 생긴 것으로 판명 | 낙과 확률이 진행도 증가분에 비례, 광이용효율 2.5 → 2.25. 디렉터 결정 12번 |
| 6 | 과위 5단계, 과실별 개화 시점 변이, 단계 개방 도입. 1월 수확이 12월의 92% | 수확 파동 펴기: 과위 지연 0·4·7·10·12일, 변이 ±4일, 창 0·30·50·70·100%. 디렉터 결정 13번 |
| 7 | 11월 환기를 고쳐 최종 확정 | 11월 환기 일정 수정 (최종 일정은 위 "기후 일정" 표) |

(차수별 세부 숫자는 여기에 적지 않고 결정에 필요한 요약만 적었어요.)

### 최종 결과 (`season_standard`)

| 지표 | 값 |
| --- | --- |
| 월별 수확 (g/m²) | 12월 865, 1월 877, 2월 603, 3월 1,029, 4월 1,045, 5월 1,423 |
| 누적 수확량 | 5,843 g/m² (약 5.8 kg/m²) |
| 포기당 과실 수 | 43.1개 |
| 과실 평균 무게 | 16.9 g |
| 평균 당도 | 9.93 °Brix |
| 첫 수확 | 12월 3일 |

처방 비교(263일, 5월 31일까지):

| 시나리오 | 누적 수확 (g/m²) | 평균 당도 (°Brix) | 평균 경도 |
| --- | --- | --- | --- |
| `season_standard` | 5,843 | 9.93 | 0.848 |
| `season_thinned` (적과) | 5,731 | 10.02 | - |
| `season_no_topdress` (웃거름 없음) | 4,747 | 9.62 | - |
| `season_n_excess` (질소 과다) | 3,789 | 9.51 | - |
| `season_k_low` (칼륨 부족) | 4,693 | 9.23 | - |
| `season_k_excess` (칼륨 과다) | 4,768 | 9.59 | 0.795 |
| `season_acid` (pH 5.0) | 279 | 8.58 | - |

모든 시나리오 확인 행이 통과했어요. 방향: 웃거름이 없으면 수확이 줄고(S7), 질소 과다는 수확·당도를 낮추고(S14), 칼륨 부족은 당도를 낮추고(S16), 칼륨 과다는 경도를 낮추고(S17, 0.848 → 0.795), 적과는 수확을 약간 줄이는 대신 당도를 올리고, 산성 토양은 작기를 거의 망쳐요(279 g/m²).

### 초안 대비 바뀐 값

`(이전 → 최종)`. 해당 명세 본문에 이미 반영했고, 최종 승인 때 디렉터 결정 11번 방식으로 일괄 승인해요.

| 키 | 이전 → 최종 | 명세 |
| --- | --- | --- |
| `house.solar_heat_gain_fraction` | 0.063 → 0.18 | house-climate |
| `house.heater_setpoint_min_degC`, `max_degC` | (없음) → 0, 30 (신설) | house-climate |
| `house.window_open_levels` | 열림/닫힘 → `[0, 0.3, 0.5, 0.7, 1.0]` (디렉터 결정 13번, 명세에는 이미 반영됨) | house-climate |
| `phenology.truss_degree_days_to_anthesis` | 600 → 350 | strawberry-phenology |
| `phenology.induction_nitrogen_curve` | `[[0,1],[1.0,1],[1.15,0.6],[1.3,0.25]]` → `[[0,1],[1.1,1],[1.3,0.6],[1.5,0.25]]` | strawberry-phenology |
| `seedling_grade.initial_induction_index` A / B / C | 0.8 / 0.4 / 0.0 → 1.0 / 0.8 / 0.3 | strawberry-phenology |
| `fruit.degree_days_to_harvest` | 450 → 350 | strawberry-fruit |
| `fruit.pollination_mean_curve` | `[[3,0.35],[8,0.65],[12,0.92],...]` → `[[3,0.2],[5,0.3],[8,0.6],[12,0.92],[25,0.92],[30,0.65],[35,0.35]]` | strawberry-fruit |
| `fruit.rank_fraction`, `rank_anthesis_delay_days`, `rank_potential_fresh_g`, `rank_abortion_weight` | 4과위 → 5과위(길이 N 일치). 분율 `[0.06,0.11,0.22,0.39,0.22]`, 지연 `[0,4,7,10,12]`, 최대 g `[35,25,17,12,8]` (디렉터 결정 13번) | strawberry-fruit |
| `fruit.anthesis_jitter_days` | 3 → 4 | strawberry-fruit |
| `fruit.abortion_rate_per_hour` → `abortion_rate_per_development_index` | 0.01/시간 → 8.0/진행도 (디렉터 결정 12번) | strawberry-fruit |
| `fruit.abortion_window_index` | 0.25 → 0.35 | strawberry-fruit |
| `fruit.abortion_satisfaction_curve` | `[[0,1],[0.6,0.5],[0.85,0],[1,0]]` → `[[0,1],[0.9,0.6],[1,0.1]]` | strawberry-fruit |
| `fruit.thinnable_max_index` | 0.3 (변경 없음, 확인함) | strawberry-fruit |
| `fruit.brix_temp_curve` | 삭제 → `brix_day_temp_score_curve`, `brix_night_temp_score_curve`, `brix_temp_score_curve`, `brix_day_weight`, `brix_night_weight` (디렉터 결정 10번) | strawberry-fruit |
| `fruit.brix_n_excess_curve` | `[[0,1],[0.15,0.95],[0.3,0.85]]` → `[[0,1],[0.3,1],[0.5,0.85]]` | strawberry-fruit. 3차: 적정 시비에서도 사치 흡수로 늘 감점되던 것을, 상한을 넘을 때부터만 감점 |
| `crop.n_luxury_uptake_ratio` | (없음, 일반 1.3) → 1.2 (신설) | strawberry-minimal, strawberry-fruit 8번 |
| `crop.n_uptake_rate_per_g_root_per_hour` | (없음, 일반 0.002) → 0.00006 (신설) | strawberry-minimal, strawberry-fruit 8번 |
| `crop.maintenance_rate_leaf_per_hour` | 0.00125 → 0.0008 | strawberry-fruit |
| `crop.leaf_senescence_rate_per_hour` | 0.0005 → 0.0003 | strawberry-fruit |
| `crop.light_use_efficiency_g_per_MJ` | 2.5 → 2.25 (디렉터 결정 12번) | strawberry-minimal |
| `crop.mn_uptake_rate_per_g_root_per_hour` (M3 값) | 0.00004 → 0.00001 | manganese, strawberry-minimal |
| `aeration.compaction_per_mm_irrigation` (M3 값) | 0.0015 → 0.001 | aeration. 2차: 작기 내내 관수하니 2월에 다짐 1 → 통기 부족 → 망간 독성이라 0.0004로. 4차: S8 과습 회귀가 약해져 범위 안에서 0.001로 |
| `root_damage.waterlog_curve` (M3 값) | `[[0,0.01],[0.2,0.004],[0.4,0.0005],[0.5,0]]` → `[[0,0.015],[0.2,0.006],[0.4,0.001],[0.5,0]]` | strawberry-minimal. 4차: 다짐 계수를 낮춘 뒤 S8 과습 손상이 약해져 회귀가 깨져서 1.5배 |

구조가 바뀐 것(해당 명세 본문에 반영):

- **과실 수요는 따라잡기**예요: 새 진행도의 목표 건물 − 지금 건물([strawberry-fruit](strawberry-fruit.md) 4번).
- **당도의 공급 입력은 크기 충족도**예요: Σ 과실 건물 ÷ Σ 목표 건물의 이동 평균. 순생장 > 0인 틱에만 갱신하고, 완화 비율은 `house.temp_mean_relax_rate_per_hour`예요. 저장 상태 `size_satisfaction_mean`(strawberry-fruit 5번).
- **N은 자기 사치 상한과 흡수 속도**를 가지고(Ca·Mg 패턴), 모든 원소의 흡수 능력에 활착 배율이 곱해져요(strawberry-minimal 3번, strawberry-fruit 8번).
- **낙과 확률은 진행도 증가분 기준**이에요: `p_tick = rate × dD × 곡선 × 과위 배율`(strawberry-fruit 6b).
- **새 필수 시나리오 키 `[crop] initial_establishment_index`**(0~1). 작기 시나리오 0, M2·M3 이행 시나리오와 이미 자란 작물 테스트 1.
- `initial_induction_index` 검증 범위 [0, 1], 과위 배열 4개의 길이 일치, 창 개방률 5단계(house-climate).
- 낙엽·뿌리 고사·음의 순생장 수축·낙과가 한 틱 작물 갱신 안에서 일어나고, 낙엽 몫(share)의 분모에 관부·과실이 들어가요(strawberry-fruit 7번).

### 기대값이 바뀌는 단위 테스트 (구현 때 디렉터 승인 목록)

프로토타입에서 기대값을 바꿔서 통과시킨 기존 단위 테스트 20개예요. CLAUDE.md 규칙에 따라 **디렉터 승인 없이 기대값을 바꾸지 않아요.** 이 목록의 승인이 구현 때 기대값 변경의 승인이에요. 원인별로 묶었어요.

| 원인 | 테스트 이름 |
| --- | --- |
| 순생장이 음수일 수 있음 (호흡, CO2, 활착) | "Crop at night has zero growth", "Finite under extreme inputs", "Product mode" |
| 낙엽·수축이 같은 호출에서 일어나고, 낙엽 몫의 분모에 관부·과실이 들어감 | "Uptake conserves pools", "Root mass does not shrink", "Root dieback moves mass", "Root unchanged when dieback zero", "Dieback returns K Ca Mg Mn" |
| 요구량에 관부·과실 포함, 흡수 능력에 활착 배율, N 전용 속도 | "Luxury content takes up nothing", "Uptake never exceeds demand", "Nutrient status with zero biomass", "Only P unavailable", "Ca/Mg own uptake rates", "Antagonism demand cap", "Antagonism capacity", "Cation uptake total = solution", "Mn uptake stops at max multiple", "Mn capacity formula", "Mn toxicity ratio ten" |
| 하우스 (닫힌 3중이 M1 식과 같지 않음) | "three closed layers match M1" |

### 진단·리포트용 기록

프로토타입 점검 중 원인을 가려내려고 추가한 기록이에요. **시뮬레이션 결과는 바꾸지 않아요.** 작기 리포트(M7)에서 쓰려고 본 구현에도 남겨요(세이브에도 저장돼요).

| 위치 | 필드 | 뜻 |
| --- | --- | --- |
| `Crop` | `last_gross_g_per_m2`, `last_respiration_g_per_m2`, `last_supply_ratio` | 이번 틱 총생산, 유지 호흡, 과실 공급 비율 |
| `HarvestRecord` | `aborted_fruits`, `thinned_fruits` | 낙과 수, 적화·적과로 딴 수 |
| `HarvestRecord` | `harvested_day_score_weighted`, `harvested_night_score_weighted`, `harvested_temp_score_weighted` | 낮·밤·합산 온도 점수 × 생체중 합 (평균 점수 계산용) |
| 화방(`Truss`) | `opened_tick`, `harvested_fruits`, `harvested_fresh_g`, `aborted_fruits`, `first_harvest_tick`, `last_harvest_tick` | 화방별 개화 틱, 수확 개수·생체중, 낙과 수, 첫·마지막 수확 틱 (수확 파동을 보는 재료) |
| `HouseState` | `inside_solar_W_per_m2`, `heating_power_W_per_m2`, `vent_path_index` | 하우스 안 일사, 난방 출력, 환기 경로 |
| CLI | `--no-hash` | `state_hash` 열을 비워 긴 실행을 빠르게 해요. 최종 해시는 그래도 찍어요 |

CLI CSV에는 하우스·작물 진단 값이 열로 나가요(`heating_power_W_per_m2`, `vent_path_index`, `last_gross_g_per_m2`, `last_respiration_g_per_m2`, `last_supply_ratio` 등).

## 구현 작업 분할 (승인 후 확정)

| 패키지 | 내용 | 파일 | 담당 모델 |
| --- | --- | --- | --- |
| 0. 골격 | 상태(화방·개별 과실 목록, 하우스 피복·창·난방)·파라미터·장부·명령 3개, 프로세스 선언, 틱 배선(f1, h1~h6), 테스트 도우미 | 헤더, `world.cpp` | 메인 (직접) |
| 0b. 검증·직렬화 | schema 5, 세이브 형식 v6 (가변 길이 목록) | `validation.cpp`, `serialization.cpp` | Sonnet |
| 1. 생물계절 | 활착, 화아분화, 화방 발달·개화, 모종 등급 | `phenology.cpp`(신규) | Sonnet |
| 1b. 하우스 기후 | 다중 피복 열수지, 창·난방 명령, 열 관성, 이동 평균, 저온 피해 | `house_climate.cpp`(신규), `temperature.cpp`, `world.cpp` 명령 | Sonnet |
| 2. 생장·과실 | 호흡, 분배, 과실 진행·품질, 수확·반출, 낙엽, 요구량 | `crop.cpp`, `fruit.cpp`(신규) | **Opus** (공급·수요 분배와 품질 적산이 얽힘) |
| 3. 탈질 | 탈질, pH H+ 소비 | `denitrification.cpp`(신규), `soil_chemistry.cpp` 일부 | Sonnet |
| 5. 데이터·도구 | schema 5 로더, params·시나리오(이행 포함), 낮 길이 곡선 생성, CLI 열, 작기 그래프 | `sim-data/**`, `sim-cli/**`, `tools/**`, `data/` | Sonnet |
| 6. 통합 검증 | 작기 시나리오, S7·S13~S17, 회귀, 보존·극한·결정론, 성능 | `data/scenarios/m4/**`, `tests/test_m4_*.cpp` | Sonnet (나머지 완료 후) |
