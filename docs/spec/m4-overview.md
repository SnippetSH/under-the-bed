# M4 개요: 딸기 완성 (생육 단계, 화방, 과실, 품질, 탈질, 모종 등급)

- 상태: 승인 (2026-11-18)
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
| 수확·적엽·적과 명령, 과숙 손실 | 익으면 자동 수확, 늙은 잎은 자동 낙엽 | M7 |
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
| 작물 | `crown_g_per_m2`, `fruits`(개별 과실), 수확 기록(크기 구간 포함) | strawberry-fruit |

### 장부 추가

`N_harvested_g`, `P_…`, `K_…`, `Ca_…`, `Mg_…`, `Mn_harvested_g`, `C_leaf_litter_g`. `N_gas_g`가 탈질로 처음 0이 아니게 돼요.

보존식: 원소별 `토양 + 체내 = 초기 + in − leached − gas − harvested`. 토양 C: `유기 C = 초기 + C_in + C_root_litter + C_leaf_litter − C_respired`.

## 하우스 기후 관리 (디렉터 결정 1번, [house-climate](house-climate.md), [ADR-0011](../decisions/0011-house-climate-in-m4.md))

난방·환기는 **플레이어가 직접** 해요. 안 하면 겨울 밤에 0 °C 근처나 아래로, 9월 한낮에 30 °C 위로 가요. 하우스는 **기본 3중 피복**이고 겹마다 양쪽 측창이 있어 **창 6개**를 열고 닫아요. 휴작기(4~8월 무렵)에 2중·4중으로 바꿀 수 있고(추가금은 M7), 피복이 많을수록 따뜻하지만 어두워요. 저온에서는 잎과 과실이 얼어요.

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
        h3. 총생산 − 유지 호흡 → 분배(과실 우선 → 잎·관부·뿌리)            ← M4
        h4. 과실 진행·품질 적산 → 자동 수확(양분 반출)                     ← M4
        h5. 흡수 (요구량에 관부·과실 포함, 활착 배율)                      ← M4 변경
        h6. 뿌리 건강도·고사 → 낙엽                                        ← M4
     i. 용탈  j. 파생값: EC  k. 이벤트 판정
  4. 하우스 CO2 갱신 (이번 틱 작물 흡수·토양 호흡·환기)                 ← M4
```

## 명령

`SetWindow{layer, side, open}`, `SetHeater{on, setpoint_degC}`, `SetCoverLayers{layers}`(휴작기만)를 추가해요([house-climate](house-climate.md)). `variant` 순서: `Noop, Irrigate, AddCompost, AddFertilizer, AddLime, Till, SetWindow, SetHeater, SetCoverLayers`. 정식은 지금처럼 시나리오 시작 시점이에요.

## M1~M3 명세 변경 (M4 승인 시 해당 문서에 반영)

| 문서 | 변경 |
| --- | --- |
| strawberry-minimal | 생장 = 총생산 × 활착 배율 − 유지 호흡, 분배는 strawberry-fruit, `leaf_allocation_fraction` 삭제, 요구량에 관부·과실, 뿌리 고사의 비율 분모에 관부·과실 포함, 단순화의 "생육 단계·호흡·노화 (M4)" 정리. 시나리오 `[crop]`은 모종 등급·재식 밀도 |
| soil-temperature | 하우스 기온 식을 [house-climate](house-climate.md)의 다중 피복 열수지로 대체, M1 `[house]` 계수 3개 삭제 |
| soil-water | 하우스 안 일사 = 일사 × 투과율^겹수 (`solar_transmittance_factor` 대체) |
| nitrogen | 탈질(f1) 추가, 기체 손실이 0이 아님. 단순화의 "탈질 없음 (M4)" 정리 |
| soil-chemistry | pH 수지에 탈질의 H+ 소비 |
| organic-matter | 낙엽 C·N이 이분해성 풀로 (뿌리 고사와 같은 규칙) |
| m1-overview, m2-overview, m3-overview | 장부·보존식에 `harvested`, `C_leaf_litter` |

## 데이터 배치

### `data/params.toml` (schema_version 5)

추가: `[phenology]`, `[fruit]`, `[denitrification]`, `[calendar] day_length_curve`, `[house]` 재구성(house-climate), `[crop]`의 분배·호흡·낙엽·과실 적정 농도·저온 피해 키, `[[seedling_grade]]`. 삭제: `crop.leaf_allocation_fraction`, `house.base_offset_degC`, `house.solar_gain_degC_per_W_per_m2`, `house.solar_transmittance_factor`.

### 시나리오 TOML (schema_version 5)

```toml
[crop]                      # 생략하면 작물 없음
seedling_grade = "B"
plants_per_m2 = 8.0

[house]                     # M1 항목 + 아래
cover_layers = 3            # 2~4
```

### 시나리오 이행 (M1~M3)

모든 시나리오를 schema 5로 올려요. 작물이 있는 M2·M3 시나리오는 `leaf_g_per_m2 = 30`, `root_g_per_m2 = 10` 대신 `seedling_grade = "B"`, `plants_per_m2 = 8`(잎 28, 관부 16, 뿌리 12 g/m²)을 써요. 모든 시나리오에 `cover_layers = 3`을 넣어요(창 닫힘·난방 없음으로 시작해 M1 하우스 식과 거의 같아요). 호흡·분배·활착이 바뀌어 생장량 수치는 달라지지만, M1~M3의 방향 테스트는 그대로 통과해야 해요. 수치 변화는 프로토타입 점검 결과에 적어요.

## 시나리오 테스트 (M4)

**작기 시나리오**(`m4/season_*`): 9월 10일(day 253) 정식, 263일(5월 31일까지, 6,312틱), 모종 B 8포기/m², 기준 토양(M3), 3중 하우스. 기후 관리는 `[[schedule]]`로 해요: 맑은 낮에는 창을 열고 저녁에 닫고, 11월부터 난방 6 °C. 정확한 시각·기간은 프로토타입 점검에서 정해요. 기준 처방(`season_standard`)은 정식 전 숙성 퇴비와 기비, 2주마다 질산칼슘·황산칼륨 웃거름, 하루 1회 관수예요. 정확한 양은 과습·건조·염류 과다가 없도록 프로토타입 점검에서 정해요.

| ID | 비교 | 기대 방향 | 기간 |
| --- | --- | --- | --- |
| **1작기** | `season_standard` | 끝까지 돌고, 11월~5월 매달 수확이 있다. 작물이 살아 있다 | 263일 |
| **처방 비교** | `season_standard` 대 S7·S14·S16 작기 시나리오, `season_acid`(pH 5.0) | 누적 수확량과 평균 당도가 처방마다 다르고, 그래프로 보인다(완료 기준) | 263일 |
| S7 | `season_standard` 대 `season_no_topdress`(웃거름 없음) | 무처리 쪽 3~5월 평균 N·K 충족도가 낮고 누적 수확량이 적다 | 263일 |
| S13 | `s13_proper` 대 `s13_waterlogged` ([denitrification](denitrification.md)) | 과습 쪽 `N_gas_g`가 크고, 적정 쪽은 0에 가깝다 | 14일 |
| S14 | `season_standard` 대 `season_n_excess`(웃거름 N 3배) | 질소 과다 쪽 첫 화방 분화와 첫 수확이 늦고, 평균 당도가 낮다 | 263일 |
| S15 | `s15_grade_a` 대 `s15_grade_c` (기준 처방) | A 쪽 첫 수확이 빠르고, 12월 31일까지 누적 수확량이 많다 | 113일 |
| S16 | `season_standard` 대 `season_k_low`(K 웃거름 없음, 초기 K 절반) | K 부족 쪽 평균 당도가 낮다 | 263일 |
| S17 | `season_standard` 대 `season_k_excess`(황산칼륨 웃거름 3배) | K 과다 쪽 평균 경도가 낮다(Ca 길항) | 263일 |
| S18 | 겨울 무관리 대 난방, 가을 창 닫힘 대 환기 ([house-climate](house-climate.md)) | 무관리 겨울: 최저 기온 < 0 °C, 잎 손실. 난방: 최저 ≈ 설정, 에너지 > 0. 가을 닫힘: 최고 > 30 °C, 화아분화 느림 | 7~14일 |
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

## 프로토타입 점검 (승인 후)

M3와 같은 방식이에요. 명세 승인 뒤 버리는 브랜치에서 돌려 초기값과 웃거름·관수 양을 맞추고, 결과와 조정한 값을 여기에 적어 최종 승인을 받아요. 최종 승인 뒤 그 프로토타입을 본 구현의 출발점으로 써요.

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
