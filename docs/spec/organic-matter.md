# 유기물 분해

- 상태: 승인 (2026-11-18)
- 마일스톤: M1
- 관련: [m1-overview](m1-overview.md), [soil-temperature](soil-temperature.md), [nitrogen](nitrogen.md)

## 목적

퇴비 선택 판단을 만들어요.

- 미숙 퇴비: 질소가 빨리 나오지만, 발열과 암모늄 급증이 따라와요. (S1)
- 고탄질비 퇴비(톱밥·수피): 분해하는 미생물이 토양의 질소를 가져가서 일시적인 질소 부족을 만들어요(부동화).
- 숙성 퇴비: 변화가 느리고 안전해요.

## 상태 변수 (셀)

| 이름 | 의미 | 단위 |
| --- | --- | --- |
| `labile_C_g_per_m2`, `labile_N_g_per_m2` | 이분해성 풀 (미숙 퇴비의 대부분) | g/m² |
| `stable_C_g_per_m2`, `stable_N_g_per_m2` | 난분해성 풀 (부식, 숙성 퇴비의 대부분) | g/m² |

C:N비는 따로 저장하지 않고 풀의 C와 N에서 계산해요.

## 파라미터

| 키 | 의미 | 단위 | 초기값 | 출처 |
| --- | --- | --- | --- | --- |
| `organic_matter.labile_decay_rate_per_hour` | 이분해성 풀 최대 분해 속도 | 1/h | 0.004 | 게임용 임의값 (최적 조건 반감기 약 7일) |
| `organic_matter.stable_decay_rate_per_hour` | 난분해성 풀 최대 분해 속도 | 1/h | 0.00002 | 게임용 임의값 (반감기 약 4년) |
| `organic_matter.humification_fraction` | 분해된 C 중 난분해성 풀로 옮겨 가는 비율 (나머지는 호흡) | - | 0.3 | 게임용 임의값 |
| `organic_matter.humus_C_to_N_ratio` | 새로 생기는 부식의 C:N비 | - | 10 | 게임용 임의값 (토양 유기물 일반 범위) |
| `organic_matter.temp_curve` | x = 토양 온도 °C | - | `[[0,0.05],[10,0.25],[20,0.55],[30,0.85],[37,1.0],[45,1.0],[55,0.5],[65,0.1]]` | 게임용 임의값 |
| `organic_matter.moisture_curve` | x = relative_water | - | `[[0,0],[0.2,0.1],[0.6,0.8],[0.9,1.0],[2.0,1.0]]` | 게임용 임의값. 포장용수량 위에서 감소하지 않는 이유: 과습 억제는 M2 통기성 계수가 맡아요 |

### 퇴비 정의 (`[[compost]]`)

| 키 | 의미 | 단위 |
| --- | --- | --- |
| `id`, `name` | 식별자, 표시 이름 | - |
| `C_g_per_kg`, `N_g_per_kg` | 생체 1 kg당 탄소·질소 | g/kg |
| `labile_C_fraction`, `labile_N_fraction` | 이분해성 풀로 가는 비율 (나머지는 난분해성) | 0~1 |

| id | 이름 | C | N | labile C | labile N | 비고 (게임용 임의값) |
| --- | --- | --- | --- | --- | --- | --- |
| `immature_manure` | 미숙 우분 퇴비 | 180 | 12 | 0.6 | 0.7 | 이분해성 C:N 약 13 |
| `mature_manure` | 숙성 우분 퇴비 | 150 | 12 | 0.1 | 0.1 | |
| `bark_compost` | 수피·톱밥 퇴비 | 250 | 5 | 0.4 | 0.3 | 이분해성 C:N 약 67, 부동화 유발 |

## 프로세스

### 투입 (명령 단계)

`AddCompost{compost_id, kg_per_m2}`는 대상 셀마다 다음을 더하고, 장부 `C_in_g`, `N_in_g`에 기록해요.

```
C = kg_per_m2 × C_g_per_kg ;  N = kg_per_m2 × N_g_per_kg
labile_C += C × labile_C_fraction ;  stable_C += C × (1 − labile_C_fraction)
labile_N += N × labile_N_fraction ;  stable_N += N × (1 − labile_N_fraction)
```

### 분해 (셀 단계 e): 이분해성 풀 → 난분해성 풀 순서로 각각

풀 p의 C를 `C_p`, N을 `N_p`라고 할 때:

```
f       = temp_curve(soil_temp) × moisture_curve(relative_water) × O2계수(=1, M2) × 분해균계수(=1, M5)
dC      = C_p × min(1, decay_rate_p × f)           # 분해된 탄소
released_N = dC × N_p / C_p                         # 분해로 풀려난 질소
humified_C = humification_fraction × dC            # 부식으로 옮겨 가는 탄소
demand_N   = humified_C / humus_C_to_N_ratio       # 새 부식이 붙잡는 질소
net_N      = released_N − demand_N
```

- **net_N ≥ 0 (무기화)**: `nh4_N += net_N`
- **net_N < 0 (부동화)**: 부족분 `need = −net_N`을 NH4+에서 먼저, 모자라면 NO3-에서 가져가요.
  - 무기 질소(NH4+ + NO3-)가 `need`보다 적으면, **분해 자체를 줄여요**: `dC, released_N, humified_C, demand_N`을 모두 `available / need` 비율로 낮춰요. 이러면 need가 정확히 available과 같아져요. (질소가 없으면 고탄질비 유기물은 분해가 멈춰요.)

풀 갱신:

```
C_p −= dC ;  N_p −= released_N
stable_C += humified_C ;  stable_N += demand_N
respired_C = dC − humified_C        → 장부 C_respired_g, 발열 (soil-temperature)
```

- `C_p = 0`이면 그 풀은 건너뛰어요.
- 환경 계수 `f`는 분해 단계 시작 시점의 토양 온도·수분으로 **한 번** 계산해 두 풀에 똑같이 써요. 발열은 두 풀의 호흡 C를 합쳐 단계 끝에 한 번 더해요. 그래서 이번 틱의 발열은 다음 틱 분해 속도에 반영돼요. (구현 중 확정, 2026-11-18)
- 난분해성 풀도 같은 식이에요(분해된 일부가 다시 난분해성 풀로 돌아와요).

### 방향 요약

| 원인 | 결과 |
| --- | --- |
| 미숙 퇴비 (이분해성 비율 높음) | 분해 빠름 → 호흡 C 많음 → 발열 큼, NH4+ 급증 |
| 고탄질비 퇴비 | net_N < 0 → 토양 무기 질소 감소 (부동화) |
| 저온·건조 | 분해 느림 |
| 온도 상승 (발열 포함) | 37~45 °C까지 분해 가속, 그 위에서 감소 |

## 보존·불변 조건

- 질소: 유기물 분해는 질소를 풀 사이에서만 옮겨요. 셀의 (유기 N + 무기 N) 합은 분해 단계 전후로 변하지 않아요.
- 탄소: `유기 C 합 = 초기 + C_in − C_respired`
- 모든 풀 ≥ 0. 부동화로 NH4+, NO3-가 음수가 되지 않아요.

## 테스트 (단위)

| 조건 | 기대 |
| --- | --- |
| 같은 C, 이분해성 비율만 다름 | 이분해성 비율이 높을수록 호흡 C가 많음 |
| C:N 13 풀 | net_N > 0, NH4+ 증가 |
| C:N 67 풀 + 무기 N 충분 | 무기 N 감소 |
| C:N 67 풀 + 무기 N 0 | 분해량 0, 풀 변화 없음 |
| 분해 단계 전후 | 셀 질소 합 동일 (1e-12 상대 오차) |

## 단순화와 비목표

- 리그닌·셀룰로오스 같은 세부 분획 없음 (두 풀로 단순화)
- 미숙 퇴비의 암모니아 휘산(기체 손실)은 M1에서 없음. 필요하면 M4의 기체 손실 항과 함께 다뤄요
- 분해균 개체군(M5)이 생기면 `분해균계수`가 1이 아닌 값이 돼요
