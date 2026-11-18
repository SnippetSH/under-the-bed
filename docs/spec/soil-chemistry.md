# 토양 화학: 양분 풀, pH 수지, 석회, EC, 유효도, 산성 장해

- 상태: 승인 (2026-11-18)
- 마일스톤: M2
- 관련: [m2-overview](m2-overview.md), [nitrogen](nitrogen.md), [strawberry-minimal](strawberry-minimal.md), [ADR-0007](../decisions/0007-acid-toxicity-and-manganese.md)

## 목적

이 게임의 핵심인 **"총량과 유효량의 분리"**(계획서 4장 9번)를 만들어요.

- 비료를 넉넉히 줬는데 pH 때문에 결핍이 생겨요(S4).
- 석회는 바로 안 듣고 며칠~몇 주에 걸쳐 pH를 바꿔요(S9).
- 많이 주면 염류(EC)가 쌓여 오히려 생장을 막아요(S10).

## 상태 변수 (셀)

| 이름 | 의미 | 단위 |
| --- | --- | --- |
| `k_g_per_m2`, `ca_g_per_m2`, `mg_g_per_m2` | 식물이 쓸 수 있는 양이온 풀 (흙에 붙은 것 + 녹은 것) | g/m² |
| `p_g_per_m2` | 식물이 쓸 수 있는 인산 풀 (P 기준) | g P/m² |
| `s_g_per_m2` | 황산 풀 (S 기준) | g S/m² |
| `ph` | 토양 pH. **M2부터 변수** | - |
| `lime_undissolved_eq_per_m2` | 아직 녹지 않은 석회 | eq/m² |
| `lime_ca_fraction`, `lime_mg_fraction` | 미용해 석회 당량 중 Ca·Mg 비율 (여러 석회가 섞이면 가중 평균) | - |
| `pending_H_eq_per_m2` | 이전 틱 작물 흡수에서 생겨 다음 pH 갱신에 반영할 산 | eq/m² |
| `ec_dS_per_m` (파생) | 토양 용액 전기전도도 | dS/m |
| `base_buffer_eq_per_m2_per_pH` (정적) | 흙 고유 완충력. 시나리오 `[soil]` (예시 5.0: 흙 240 kg/m² 기준 약 2 cmol/kg per pH, 게임용 임의값) | eq/m² per pH |

## 파라미터

### 이온 (`[ions]`): 원소 기준 1 당량의 무게

| 키 | 값 (g/eq) | 근거 |
| --- | --- | --- |
| `nh4_N_g_per_eq`, `no3_N_g_per_eq`, `no2_N_g_per_eq` | 14.007 | N 원자량, 1가 |
| `k_g_per_eq` | 39.098 | K 원자량, 1가 |
| `ca_g_per_eq` | 20.039 | Ca 원자량 ÷ 2 |
| `mg_g_per_eq` | 12.153 | Mg 원자량 ÷ 2 |
| `p_g_per_eq` | 30.974 | H2PO4- 1가로 단순화 |
| `s_g_per_eq` | 16.03 | SO4 2- (S 원자량 ÷ 2) |

### 용액 비율 (`[soil_chemistry]`)

M1의 `leaching.nh4_solution_fraction`과 같은 방식이에요. M3에서 양이온 교환으로 바뀌어요.

| 키 | 의미 | 초기값 | 출처 |
| --- | --- | --- | --- |
| `k_solution_fraction` | 풀 중 녹아 있는 비율 | 0.03 | 게임용 임의값 (2026-11-18 프로토타입 점검으로 조정) |
| `ca_solution_fraction` | | 0.01 | 게임용 임의값 (2026-11-18 프로토타입 점검으로 조정) |
| `mg_solution_fraction` | | 0.02 | 게임용 임의값 (2026-11-18 프로토타입 점검으로 조정) |
| `p_solution_fraction` | | 0.002 | 게임용 임의값 (S4에서 고pH일 때 흡수 능력이 수요보다 작아지도록 잡은 값 (2026-11-18 프로토타입 점검으로 조정)) |
| `s_solution_fraction` | | 0.8 | 게임용 임의값 (황산은 잘 녹고 잘 씻김) |
| `nitrification_H_eq_per_eq_N` | 질산화된 N 1 eq당 생기는 H+ | 2.0 | 화학량론 (NH4+ + 2O2 → NO3- + 2H+ + H2O) |
| `uptake_H_eq_per_eq_NH4` | 작물이 NH4+ 1 eq 흡수할 때 내놓는 H+ | 1.0 | 전하 균형 |
| `uptake_H_eq_per_eq_NO3` | NO3- 1 eq 흡수할 때 (음수 = OH- 방출) | -1.0 | 전하 균형 |
| `organic_C_buffer_eq_per_g_C_per_pH` | 유기 C 1 g/m²당 완충력 | 0.002 | 게임용 임의값 |
| `ph_min`, `ph_max` | pH 계산 하한·상한 | 3.0, 10.0 | 게임용 임의값 (극한 입력 안전장치) |
| `lime_dissolution_rate_per_hour` | 미용해 석회의 최대 시간당 용해 비율 | 0.004 | 게임용 임의값 (최적 조건 반감기 약 7일) |
| `lime_dissolution_ph_curve` | x = pH, 산성일수록 빨리 녹음 | `[[4.5,1.0],[5.5,0.7],[6.5,0.3],[7.5,0.05],[8.0,0.0]]` | 게임용 임의값 |
| `lime_dissolution_moisture_curve` | x = relative_water | `[[0,0],[0.3,0.3],[0.7,1.0],[2.0,1.0]]` | 게임용 임의값 |
| `ec_meq_per_L_per_dS_per_m` | EC 1 dS/m에 해당하는 이온 농도 | 10.0 | 경험식 (용액 EC 1 dS/m ≈ 양이온 10 meq/L) |

### 유효도 곡선 (`[availability]`, x = pH, y = 0~1)

| 키 | 초기값 | 근거 |
| --- | --- | --- |
| `n_ph_curve` | `[[4.0,0.5],[5.0,0.9],[5.5,1.0],[8.0,1.0],[9.0,0.8]]` | 일반 유효도 도표의 모양, 게임용 임의값 |
| `p_ph_curve` | `[[4.5,0.2],[5.5,0.6],[6.0,0.95],[6.8,1.0],[7.5,0.35],[8.0,0.1],[8.5,0.05]]` | 같은 근거. 저pH(Fe·Al 고정), 고pH(Ca 고정)에서 감소 |
| `k_ph_curve` | `[[4.0,0.5],[5.0,0.8],[5.8,1.0],[9.0,1.0]]` | 같은 근거 |
| `ca_ph_curve` | `[[4.0,0.3],[5.0,0.55],[5.5,0.75],[6.5,1.0],[9.0,1.0]]` | 같은 근거 |
| `mg_ph_curve` | `[[4.0,0.3],[5.0,0.55],[5.5,0.75],[6.5,1.0],[9.0,1.0]]` | 같은 근거 |

### 산성 장해 (ADR-0007)

| 키 | 의미 | 초기값 | 출처 |
| --- | --- | --- | --- |
| `root_damage.acid_curve` | x = pH, y = 시간당 뿌리 건강도 손상 비율 (알루미늄 독성 근사) | `[[4.0,0.006],[4.6,0.0018],[5.0,0.0006],[5.4,0.0001],[5.6,0.0]]` | 게임용 임의값. 알루미늄이 대략 pH 5.0~5.5 아래에서 급증하는 경향을 반영 |

### 석회 정의 (`[[lime]]`)

| id | 이름 | 당량 (eq/kg) | Ca 비율 | Mg 비율 | 근거 |
| --- | --- | --- | --- | --- | --- |
| `calcium_carbonate` | 탄산석회 | 20.0 | 1.0 | 0.0 | CaCO3 1 kg = 20 eq |
| `dolomite` | 고토석회 | 21.7 | 0.6 | 0.4 | CaMg(CO3)2 1 kg ≈ 21.7 eq, 당량 기준 Ca·Mg 비율은 게임용 단순화 |

### 비료·퇴비 정의 확장

`[[fertilizer]]`, `[[compost]]`에 `k_g_per_kg`, `ca_g_per_kg`, `mg_g_per_kg`, `p_g_per_kg`, `s_g_per_kg`, `direct_acid_eq_per_kg`(질산화·흡수로 설명되지 않는 직접 산·염기 효과, 음수는 염기)를 추가해요.

| 비료 | 추가 성분 (g/kg) | 근거 |
| --- | --- | --- |
| `ammonium_sulfate` | S 240 | 제품 일반 성분 |
| `urea` | 없음 | |
| `calcium_nitrate` | Ca 190 | 제품 일반 성분 |
| `potassium_sulfate` (신규, 황산칼륨) | K 415, S 180 | 제품 일반 성분 K2O 50% |
| `superphosphate` (신규, 과인산석회) | P 87, Ca 180, S 110 | 제품 일반 성분 P2O5 20% |
| `magnesium_sulfate` (신규, 황산고토) | Mg 98, S 130 | 제품 일반 성분 |

퇴비의 K·Ca·Mg·P 함량은 게임용 임의값으로 넣고, `tmp.md`의 "퇴비 성분 공개 자료로 다시 잡기" 항목에서 함께 다뤄요.

## 프로세스

### 투입 (명령 단계)

- 비료·퇴비: 각 원소 풀에 `양 × 함량`을 더하고 원소별 `*_in_g` 장부에 기록. `direct_acid_eq_per_kg × 양`은 이번 틱 H+에 더해요. (퇴비의 K 등은 투입 즉시 무기 풀로 들어간다고 단순화)
- 석회 `AddLime{lime_id, g_per_m2}`: `lime_undissolved_eq += g/1000 × eq_per_kg`. Ca·Mg 비율은 기존 미용해 석회와 당량 가중 평균.

### 석회 용해 → pH 갱신 (셀 단계 g)

```
dissolved_eq = lime_undissolved × min(1, rate × ph_curve(pH) × moisture_curve(relative_water))
lime_undissolved −= dissolved_eq
ca += dissolved_eq × lime_ca_fraction × ca_g_per_eq        → ca_in_g
mg += dissolved_eq × lime_mg_fraction × mg_g_per_eq        → mg_in_g

net_H_eq = 이번 틱 질산화 H+ + 이번 틱 직접 산(명령) + pending_H(지난 틱 흡수) − dissolved_eq
buffer   = base_buffer + organic_C_buffer × 유기 C
pH      −= net_H_eq / buffer          → ph_min~ph_max로 제한
pending_H = 0                          (흡수 단계가 다시 채움)
```

- 방향: 질산화와 암모늄 흡수는 pH를 낮추고, 질산 흡수와 석회는 올려요. 유기물이 많은 흙은 pH가 덜 흔들려요.
- 석회는 산성일 때 빨리 녹고, pH가 올라갈수록 느려져요. 그래서 과하게 넣어도 pH 8 위로는 잘 안 올라가요.
- M3에서 완충력에 CEC 항이 추가돼요.

### 유효량

```
solution_X  = 풀_X × X_solution_fraction             (N은 M1과 같이 NH4는 nh4_solution_fraction, NO3는 1)
available_X = solution_X × X_ph_curve(pH)
```

작물 흡수는 `available_X`를 넘지 못해요([strawberry-minimal](strawberry-minimal.md)).

### 용탈 확장 (셀 단계 i)

M1과 같은 배수 비율 `f`로, 각 원소의 **용액 부분**만 빠져요: `X −= 풀_X × X_solution_fraction × f` → `*_leached_g`.

### EC (셀 단계 j)

```
cation_meq = Σ (용액 NH4, K, Ca, Mg) g / g_per_eq × 1000
anion_meq  = Σ (용액 NO3, NO2, S, P) g / g_per_eq × 1000
ec_dS_per_m = (cation_meq + anion_meq) / 2 / water_mm / ec_meq_per_L_per_dS_per_m
```

- `water_mm`는 L/m²이므로 나누면 meq/L예요. 흙이 마르면 같은 양에서도 EC가 올라가요.
- `water_mm`가 0에 가까우면 EC가 매우 커져요. 계산은 `max(water_mm, ε_mm)`로 해요. `ε_mm`는 파라미터 `soil_chemistry.ec_min_water_mm` (초기값 1.0, 게임용 임의값).

## 보존·불변 조건

- 원소별: `토양 풀 + 작물 체내 = 초기 + in − leached` (N은 기체 손실 포함). 석회의 Ca·Mg는 녹을 때 `*_in_g`로 기록하므로, 미용해 석회는 장부 밖이에요.
- 산: `H_eq_net` 장부에 net_H를 누적. pH 변화 합 × 완충력과 대조하는 테스트는 완충력이 일정한 경우에만 해요.
- pH는 ph_min~ph_max, EC ≥ 0, 모든 풀 ≥ 0.

## 테스트 (단위)

| 조건 | 기대 |
| --- | --- |
| 질산화로 N 1 eq 생성 | pH가 2/buffer만큼 정확히 내려감 |
| 같은 산, 유기 C 많음 | pH 변화폭이 작음 |
| 석회 투입 직후 | pH 변화 없음 (명령 단계에서는 미용해 풀만 늘어남) |
| 석회, pH 5.0 대 7.0 | 5.0 쪽이 빨리 녹음 |
| 석회 과다 | pH가 8 근처에서 멈춤 |
| 같은 풀, pH 6.5 대 8.0 | P 유효량이 8.0에서 작음 |
| 같은 이온 양, 수분 절반 | EC 약 2배 |
| 배수 | 용액 비율이 큰 원소(S)가 작은 원소(P)보다 상대 손실이 큼 |
| 산성 장해 곡선 | pH 5.6 이상 0, 낮을수록 증가 |

## 단순화와 비목표

- pH는 셀당 값 하나예요. 근권 pH(뿌리 주변만 다른 pH)는 없어요.
- 탄산칼슘 침전, 석고 생성 같은 2차 반응은 없어요.
- 나트륨, 염소는 없어요(염류는 비료 성분으로만).
- 인산 고정의 시간 지연과 망간은 M3예요.
