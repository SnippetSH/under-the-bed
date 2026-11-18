# 망간: 유효태(교환성·용액)·산화물 풀, 산화·환원, 결핍과 과잉

- 상태: 승인 (2026-11-18, 2차 프로토타입 점검 후 개정본 재승인)
- 마일스톤: M3
- 관련: [ADR-0007](../decisions/0007-acid-toxicity-and-manganese.md), [ADR-0010](../decisions/0010-manganese-oxide-pool.md), [cation-exchange](cation-exchange.md), [aeration](aeration.md), [strawberry-minimal](strawberry-minimal.md)

## 목적

ADR-0007 결정 2번을 구현하고, ADR-0010으로 보완해요. 망간은 **pH만 봐서는 원인을 알 수 없는** 요소예요.

- 산성 토양, 그리고 **pH가 정상이어도 과습(환원)**이면 산화물이 녹아 나와 용액 망간이 늘어요. 과잉이면 독성이 생겨요.
- 알칼리 토양에서는 망간이 빠르게 산화물로 굳어 결핍이 생겨요.
- **석회는 산화를 빠르게 하고 흡착을 세게 해서** 망간 과잉을 풀어요. pH를 바꾸지 않는 칼슘 비료는 이 효과가 없어요.
- → 판단: pH 측정만으로 끝내지 않고 **토양 검정·엽분석**으로 망간을 봐야 해요. 대응도 원인별로 달라요(석회 / 배수·경운 / 황산망간 시비 또는 산성 비료).
- M5 토양 소독이 용액 망간을 늘리는 효과("소독 직후 정식 → 망간 과잉")는 M5에서 산화물 풀의 환원으로 연결해요(ADR-0007, ADR-0010).

## 상태 변수

### 셀

| 이름 | 의미 | 단위 |
| --- | --- | --- |
| `mn_g_per_m2` | **유효태 망간(Mn2+)**: 교환성 + 용액. 교환 경쟁·흡수·용탈 대상 | g Mn/m² |
| `mn_solution_g_per_m2` | 그중 녹은 것 ([cation-exchange](cation-exchange.md)) | g Mn/m² |
| `mn_oxide_g_per_m2` (신규) | 산화물 망간(MnO2 등). 녹지 않고 교환 자리에도 없음. 흡수·용탈되지 않음 | g Mn/m² |

토양 Mn 총량 = `mn_g_per_m2 + mn_oxide_g_per_m2`.

### 작물

| 이름 | 의미 | 단위 |
| --- | --- | --- |
| `plant_Mn_g_per_m2` | 체내 망간 | g/m² |
| 파생 `plant_Mn_ratio` | `plant_Mn / (target_Mn × (leaf + root))`. 독성 곡선의 x | - |
| 파생 `sufficiency_Mn`, `excess_index_Mn` | M2 원소와 같은 정의: `min(1, ratio)`, `max(0, ratio − 1)`. 과잉 지수는 상한 1.3배가 아니라 수요 상한 30배까지 커질 수 있어요 (2026-11-18 프로토타입 점검에서 명시) | - |
| `stress_hours[NutrientMn]`, `stress_hours[ToxicityMn]` | 결핍·과잉이 생장을 가장 크게 제한한 시간 | h |

## 파라미터

`manganese.mobility_ph_curve`, `manganese.mobility_o2_curve`, `cation_exchange.mn_retention_factor`는 **삭제**해요(아래 산화·환원과 pH 의존 흡착력으로 대체).

| 키 | 의미 | 초기값 | 출처 |
| --- | --- | --- | --- |
| `ions.mn_g_per_eq` | Mn2+ 1 당량 무게 | 27.469 | Mn 원자량 54.938 ÷ 2 |
| `manganese.retention_ph_curve` | x = pH, y = 교환 세기 w_Mn. 산성에서는 약한 정전기 흡착, pH가 오르면 탈양성자화로 특이 흡착 자리가 생겨 세짐 | `[[4.5,450],[5.0,500],[5.1,900],[5.3,1500],[6.5,1500],[7.5,1800],[8.5,2000]]` | 디렉터 지시 범위(pH 5.0 이하 약 500, 오를수록 1500 이상). pH 5.0~5.3에서 가파르게 올라 석회의 Ca 밀어내기보다 흡착 증가가 커지도록 (2026-11-18 프로토타입 점검으로 조정: H, 디렉터 결정) |
| `manganese.oxidation_rate_per_hour` | Mn2+ → 산화물 최대 시간당 비율 (곡선 1 기준) | 0.002 | 게임용 임의값 (pH 6.5·통기 충분에서 반감기 약 2주) |
| `manganese.oxidation_ph_curve` | x = pH, y = 산화 속도 배율. **지수 모양을 구간 선형으로 근사**. pH 5.0~5.8에서 가파름 | `[[4.5,0.005],[5.0,0.02],[5.3,0.06],[5.5,0.12],[5.8,0.3],[6.0,0.5],[6.5,1.0],[7.0,2.0],[7.5,4.0],[8.5,10.0]]` | 게임용 임의값. 화학 산화 + 망간 산화 미생물 활성이 pH 0.3~0.5 상승에 급증하는 경향 |
| `manganese.oxidation_o2_curve` | x = aeration_index, y = 산화 속도 배율 (산소 필요) | `[[0,0],[0.3,0.2],[0.6,0.8],[0.8,1.0],[1,1.0]]` | 게임용 임의값 |
| `manganese.reduction_rate_per_hour` | 산화물 → Mn2+ 최대 시간당 비율 (곡선 1 기준) | 0.00006 | 게임용 임의값. 기준 토양(pH 6.2, 통기 충분, 유효태 5 g, 산화물 100 g)에서 산화량 ≈ 환원량이 되게 (5 × 0.002 × 0.7 ≈ 100 × 0.00006 × 1.24) |
| `manganese.reduction_ph_curve` | x = pH, y = 환원 용해 배율 (산성일수록 큼) | `[[4.5,6.0],[5.0,3.0],[5.5,1.8],[6.5,1.0],[7.5,0.5],[8.5,0.3]]` | 게임용 임의값 |
| `manganese.reduction_o2_curve` | x = aeration_index, y = 환원 배율 (과습일수록 큼) | `[[0,20.0],[0.2,10.0],[0.4,4.0],[0.6,1.5],[0.8,1.0],[1,1.0]]` | 게임용 임의값. 과습 손상 곡선과 같은 구간에서 커지게 |
| `crop.target_Mn_g_per_g` | 적정 체내 농도 | 0.0001 | 딸기 잎 분석 적정 범위 대략 50~200 mg/kg |
| `crop.mn_uptake_rate_per_g_root_per_hour` | 뿌리 1 g당 용액 Mn에서 시간당 가져가는 비율 | 0.00004 | 게임용 임의값. 중성 토양(s11_neutral) 21일 뒤 체내 비율 약 1.7, 기준 시나리오 망간 독성 0시간이 되게 (2026-11-18 프로토타입 점검으로 조정: 초안 0.0002 → 2차 0.00002 → 3차 0.00004) |
| `crop.mn_max_uptake_ratio` | 적정 농도의 몇 배까지 쌓일 수 있는지 | 30 | 게임용 임의값. 망간은 수동적으로 쌓여 독성까지 가요 |
| `crop.mn_toxicity_curve` | x = 체내 Mn 비율, y = 생장 계수 | `[[0,1],[5,1],[10,0.7],[20,0.3],[30,0.1]]` | 게임용 임의값 (약 500 mg/kg부터 장해) |
| 비료 `manganese_sulfate` (신규, 황산망간) | Mn 320, S 190 g/kg | | 제품 일반 성분 (MnSO4·H2O) |
| 비료·퇴비 `mn_g_per_kg` | 기존 비료 0, 퇴비 미숙 0.3·숙성 0.4·수피 0.2 | | 게임용 임의값 |
| 시나리오 `[soil] mn_g_per_m2` | 초기 유효태 | 5.0 | 게임용 임의값 (교환성 Mn 약 20 mg/kg) |
| 시나리오 `[soil] mn_oxide_g_per_m2` (신규) | 초기 산화물 | 100.0 | 게임용 임의값 (환원성 Mn 약 400 mg/kg). 기준 토양에서 산화·환원이 대략 평형이 되게 |

## 프로세스

### 1. 산화·환원 (셀 단계 f2, 교환 계산 **앞**)

```
oxidized = mn × min(1, oxidation_rate × oxidation_ph_curve(pH) × oxidation_o2_curve(aeration))
reduced  = mn_oxide × min(1, reduction_rate × reduction_ph_curve(pH) × reduction_o2_curve(aeration))
mn       += reduced − oxidized
mn_oxide += oxidized − reduced
```

- 두 이동량은 같은 시작 값으로 계산해요(순서 의존 없음).
- 산화는 유효태 총량에서 빼고 **용액도 비례 차감**해요([cation-exchange](cation-exchange.md)의 기본 규칙). 환원으로 나온 Mn2+는 **용액으로** 들어가요.
- 방향:
  - pH가 0.3~0.5만 올라도(5.0 → 5.5) 산화가 몇 배 빨라지고 환원은 줄어서 유효태가 떨어져요. 석회 효과의 주된 원인이에요.
  - 과습하면 산화가 멈추고 환원이 크게 늘어 산화물이 녹아 나와요. 물이 빠지면 천천히 다시 산화돼요.
  - 알칼리에서는 빠르게 산화돼 유효태가 줄어 결핍이 생겨요.

### 2. 흡착 세기와 분배 (셀 단계 f2, 양이온 교환 안에서)

```
w_Mn = retention_ph_curve(pH)
```

- 이 세기로 다른 양이온과 같은 빈자리 경쟁·완화를 해요([cation-exchange](cation-exchange.md)).
- 다른 양이온의 세기는 상수예요. 망간만 pH에 따라 세져요(전이금속의 특이 흡착).
- 결과: Ca 비료(pH 변화 없음)는 Ca가 자리를 채워 Mn을 약간 밀어낼 뿐이에요. 석회는 pH를 올려 Mn을 더 세게 붙잡고 산화도 빠르게 해서, 밀려나는 효과보다 크게 줄여요.

### 3. 유효량

`available_Mn = mn_solution` (별도 pH 유효도 곡선 없음. pH 효과는 산화·흡착에 이미 들어 있어서 두 번 세지 않아요).

### 4. 흡수 (단계 h)

```
demand_Mn   = max(0, target_Mn × mn_max_uptake_ratio × (leaf + root) − plant_Mn)
capacity_Mn = available_Mn × min(1, mn_uptake_rate × root × root_health) × water × salt
uptake_Mn   = min(demand_Mn, capacity_Mn)
```

- 다른 원소와 달리 **수요 상한이 높아서(30배) 공급이 많으면 계속 쌓여요**. 체내 농도는 "공급 속도 대 생장 희석"으로 정해져요. 그래서 흡수 속도 계수가 따로 있어요.
- 길항 배율은 적용하지 않아요(단순화).

### 5. 스트레스 (단계 h)

```
nutrient_X  = nutrient_stress_curve(sufficiency_X)      X ∈ {N, P, K, Ca, Mg, Mn}
mn_toxicity = mn_toxicity_curve(plant_Mn_ratio)
combined    = minimum: min(water, salt, nutrient_N … nutrient_Mn, root, mn_toxicity)
              product: water × salt × Π_X nutrient_X × root × mn_toxicity
```

- `StressFactor`에 `NutrientMn`, `ToxicityMn`을 **맨 뒤(Root 다음)**에 추가해요. 기존 인덱스와 동률 규칙이 그대로 유지돼요.

### 6. 투입·반환·용탈

- 비료·퇴비의 `mn_g_per_kg`은 유효태 총량과 용액에 더하고 `Mn_in_g`에 기록해요.
- 뿌리 고사 시 체내 Mn도 M2 규칙대로 비율만큼 토양(유효태 총량·용액)으로 돌아가요.
- 용탈: 용액 × 배수 비율 → `Mn_leached_g`. 흡수 → `Mn_uptake_g`. 산화물은 용탈·흡수되지 않아요.

## 보존·불변 조건

- Mn: `유효태 + 산화물 + 체내 = 초기(유효태 + 산화물) + Mn_in − Mn_leached`. 산화·환원은 계 안의 이동이라 장부 항이 없어요.
- `0 ≤ mn_solution ≤ mn_g_per_m2`, 산화물 ≥ 0, 체내 ≥ 0.

## 테스트 (단위)

| 조건 | 기대 |
| --- | --- |
| 같은 유효태, pH 5.0 대 5.5 대 6.5 | 산화량 6.5 > 5.5 > 5.0, 환원량은 반대 |
| 같은 유효태, pH 5.0 대 6.5 대 8.0 | 평형 용액 Mn 5.0 > 6.5 > 8.0 (흡착 세기) |
| 같은 pH 6.5, 통기 0.1 대 0.8 | 0.1 쪽 환원량 큼, 산화량 작음 |
| 통기 회복 | 유효태가 바로 돌아가지 않고 산화 속도로 서서히 줄어듦 |
| 같은 Ca 당량, 석회(pH 상승) 대 질산칼슘(pH 변화 없음), 산성 토양 14일 | 석회 쪽 용액 Mn과 유효태가 작음 |
| 산화·환원 속도 극단 (곡선 × 속도 ≥ 1) | 원천 풀이 0이 되고 음수가 아님 |
| 체내 Mn이 적정의 30배 | 흡수 0 |
| 체내 Mn 비율 10 | 생장 계수 0.7, ToxicityMn이 가장 작으면 stress_hours에 기록 |
| 황산망간 투입 | 유효태·용액·`Mn_in_g` 증가, S도 증가. 산화물은 그대로 |

## 시나리오 (S11, [m3-overview](m3-overview.md))

| 시나리오 | 조건 |
| --- | --- |
| `s11_neutral` | pH 6.5, 적정 관수 |
| `s11_acid` | pH 5.2, 적정 관수 |
| `s11_wet` | pH 6.5, 다짐 0.8 + 하루 25 mm (S8 과습과 같은 물 조건) |
| `s11_alkaline` | pH 7.8, 적정 관수 |

기대 방향 (21일): 평균 용액 Mn은 acid > neutral, wet > neutral, neutral > alkaline. 최종 체내 Mn 비율도 같은 순서. alkaline의 Mn 충족도 < neutral의 Mn 충족도.

**S11 석회 조건 (2026-11-18 추가)**: M2의 `s9_lime` 대 `s9_none`(산성 토양 pH 5.0, 14일)에서 석회 쪽이 ① 14일 평균 용액 Mn이 작고 ② 최종 유효태 Mn이 작고 ③ 최종 체내 Mn 비율이 작아요(③은 3차 점검 후 추가: 교훈이 작물까지 이어지는지). "산성 망간 과잉 → 석회"라는 교훈을 고정하는 조건이에요.

## 단순화와 비목표

- 산화물은 풀 하나예요. 산화 상태(Mn3+·Mn4+)나 광물 종류는 구분하지 않아요.
- 산화의 미생물 몫은 pH·산소 곡선 안에 들어 있어요. 망간 산화균 개체군은 두지 않아요(M5에서 미생물이 생겨도 따로 연결하지 않고, 소독 효과만 환원 쪽에 연결해요).
- 철 결핍 유발(망간 과잉의 2차 효과), Ca·Mg와의 흡수 경쟁은 없어요.
- 망간 과잉 증상(갈색 반점)과 결핍 증상(엽맥 사이 황화) 화면 표현은 증상 매핑(M4~M7)에서 해요.
