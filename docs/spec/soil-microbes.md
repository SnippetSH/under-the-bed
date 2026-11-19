# 토양 미생물: 개체군 동역학, 유익균 투입, 토양 병해(시들음병·역병)

- 상태: 승인 (2026-11-19)
- 마일스톤: M5a ([m5-overview](m5-overview.md))
- 관련: [organic-matter](organic-matter.md)(분해균계수), [nitrogen](nitrogen.md)(질산화), [phosphorus](phosphorus.md)(고정 인산), [strawberry-minimal](strawberry-minimal.md)(스트레스·뿌리 건강도), [disinfection](disinfection.md)(사멸), [ADR-0012](../decisions/0012-m5-split-and-population-index.md)
- 참고 자료: [references/m5-soil-disinfection.md](../../references/m5-soil-disinfection.md)(2차, 원문 확인), [references/m5-microbes-disease.md](../../references/m5-microbes-disease.md)(1차)
- 2026-11-19 2차 근거 조사 반영: 열 사멸 곡선(시들음병균 5~20배 강화, 질산화균 35~40 °C부터), 역병 적온 20~25 °C, 시들음병 pH 배율, 1·2단계 질산화균 민감도 차이 삭제(근거 없음)

## 목적

흙 속 미생물을 **늘고 주는 개체군**으로 만들어요. 플레이어는 병원균을 줄이려는 모든 행동(소독, 과습 피하기, pH 교정)이 유익균에도 닿는다는 것과, 유익균을 살리는 방법(퇴비, 유익균 투입, 인산 절제)을 판단해요.

- 소독은 병원균만 골라 죽이지 못해요. 질산화균이 죽으면 질소가 암모늄에 머물러요(알칼리 토양에선 아질산까지).
- 길항균은 먹이(유기물)가 있어야 버텨요. 퇴비가 많은 흙은 병이 덜 번져요.
- 균근균은 뿌리가 있어야 늘고, 유효 인산이 많으면 줄어요. 대신 건조·염류를 덜 아프게 해요.
- 시들음병균은 고온·산성, 역병균은 과습·저지온에서 뿌리를 감염시켜요. 감염은 되돌릴 수 없고 뿌리 건강도를 계속 깎아요.

## 무리 (고정 순서)

| 순서 | id | 이름 | 수용력(먹이·숙주) | 활동이 쓰이는 곳 |
| --- | --- | --- | --- | --- |
| 0 | `decomposer` | 분해균 | 이분해성 유기 C | 유기물 분해 `분해균계수` |
| 1 | `nitrifier_aob` | 질산화균 1단계 | 상수 1 | 질산화 1단계 속도 |
| 2 | `nitrifier_nob` | 질산화균 2단계 | 상수 1 | 질산화 2단계 속도 |
| 3 | `antagonist` | 근권 길항균 (Bacillus·Trichoderma) | 이분해성 유기 C | 병원균과 경쟁, 감염 보호 |
| 4 | `mycorrhiza` | 균근균 | 뿌리 건물 × 유효 인산 억제 | 고정 인산 가용화, 건조·염류 완화 |
| 5 | `fusarium` | 시들음병균 | 부생 수용력 + 감염 뿌리 | 작물 감염 |
| 6 | `phytophthora` | 역병균 | 부생 수용력 + 감염 뿌리 | 작물 감염 |

순서는 세이브·CSV 열 순서예요. M5b의 `anthracnose`(토양 잔재 접종원)는 7번으로 뒤에 붙어요.

## 상태 변수

| 이름 | 위치 | 의미 | 단위 | 범위 |
| --- | --- | --- | --- | --- |
| `<id>_index` (7개) | 셀 | 상대 밀도. 1 = 그 토양에서의 최대 정착 수준 | - | 0~1 |
| `infection_fusarium_index`, `infection_phytophthora_index` | 작물 | 포기 조직(관다발·관부·뿌리) 감염 정도. 줄지 않아요 | - | 0~1 |
| `disease_onset_flags` | 작물 | 병별로 `DiseaseOnset` 이벤트를 이미 냈는지 (이벤트 중복 방지) | - | bool |

**공개 여부 (2026-11-19 디렉터 결정)**: 위 상태와 아래 파생값은 모두 **플레이어 비공개 진짜 값**이에요. CLI·테스트·개발 그래프에서만 보이고, 플레이어는 증상(`DiseaseOnset`, 표현은 M7)·미생물 검정(M7)·작기 리포트로 알아요([m5-overview](m5-overview.md) 12장).

파생값(스냅샷·CSV, 비공개): 무리별 이번 틱 적합도 `suitability`, 수용력 `capacity`, 활동 계수(분해·1단계·2단계), 균근균 완화량, 이번 틱 소독 사멸량.

## 파라미터 (`[microbes]`, `[microbes.<id>]`, 전부 게임용 임의값. 목표값은 m5-overview 5장)

### 공통 `[microbes]`

| 키 | 의미 | 초기값 |
| --- | --- | --- |
| `capacity_floor_index` | 수용력 하한 (0 나눗셈 방지) | 0.01 |
| `decomposer_activity_curve` | x = 분해균 지수, y = 분해균계수 | `[[0,0.15],[0.5,0.7],[1,1]]` |
| `aob_activity_curve` | x = 1단계균 지수, y = 1단계 속도 배율 | `[[0,0],[0.2,0.4],[1,1]]` |
| `nob_activity_curve` | x = 2단계균 지수, y = 2단계 속도 배율 | `[[0,0],[0.2,0.4],[1,1]]` |
| `antagonist_protection_factor` | 길항균 지수 1일 때 감염 압력 감소 비율 | 0.5 |
| `mycorrhiza_p_release_factor` | 균근균 지수 1일 때 고정 → 유효 인산 속도 증가 배율 | 2.0 |
| `mycorrhiza_water_relief_factor` | 균근균 지수 1일 때 수분 스트레스 결손분 완화 비율 | 0.3 |
| `mycorrhiza_salt_relief_factor` | 〃 염류 스트레스 | 0.3 |
| `moist_heat_curve` | x = relative_water, y = 열 사멸 배율 (젖은 흙이 잘 죽음) | `[[0.2,0.3],[0.5,0.7],[0.8,1.0]]` |

### 무리별 `[microbes.<id>]`

| 키 | 의미 |
| --- | --- |
| `growth_rate_per_hour` | 최대 성장률 r |
| `temp_curve`, `moisture_curve`, `ph_curve`, `o2_curve` | x = 토양 온도 °C / relative_water / pH / aeration_index, y = 0~1 적합도 |
| `base_death_rate_per_hour` | 자연 사멸 (적합도와 무관) |
| `heat_kill_curve` | x = 토양 온도 °C, y = 시간당 열 사멸 비율 (태양열 소독, [disinfection](disinfection.md)) |
| `immigration_rate_per_hour` | 주변(깊은 층·공기)에서 들어오는 재정착. `× (1 − index)` |
| `competition` | 다른 무리 id → 경쟁 계수 α (그 무리가 이 무리의 수용력을 차지하는 비율) |
| 수용력 키 | 아래 표 |

| id | r /h | 온도 적합 | 기타 적합 | 자연 사멸 /h | 열 사멸(시간당) | 유입 /h | 수용력 | 경쟁 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| decomposer | 0.05 | `[[0,0.1],[10,0.5],[25,1],[38,1],[45,0.3]]` | 수분 `[[0.1,0.2],[0.5,1],[1.2,1],[1.8,0.6]]` | 0.0005 | `[[40,0],[45,0.005],[50,0.03],[55,0.1]]` (다조멧 뒤 바이오매스 54~75% 감소·5~8주 회복 [S13]에 맞춰 덜 죽음) | 0.002 | `decomposer_food_curve` x = 이분해성 C g/m² `[[0,0.4],[10,0.8],[30,1]]` | - |
| nitrifier_aob | 0.008 | `[[0,0],[5,0.2],[25,1],[35,1],[40,0.3]]` | pH `[[4.5,0.2],[5.5,0.6],[6.5,1],[8.5,1]]`, O2 `[[0,0.1],[0.3,0.6],[0.5,1]]` | 0.0002 | `[[33,0],[36,0.005],[40,0.02],[45,0.06],[50,0.15]]` (태양열 35~40 °C에서도 생균 −99% [S19]) | 0.0005 | 1 | - |
| nitrifier_nob | 0.008 | `[[0,0],[5,0.2],[25,1],[35,1],[40,0.3]]` | pH `[[4.5,0.2],[5.5,0.6],[6.5,1],[7.5,0.8],[8.5,0.5]]`, O2 `[[0,0.1],[0.3,0.6],[0.5,1]]` | 0.0002 | 1단계와 같음 | 0.0005 | 1 | - |
| antagonist | 0.02 | `[[5,0.2],[20,1],[35,1],[42,0.4]]` | 수분 `[[0.2,0.3],[0.5,1],[1.3,0.8]]` | 0.0003 | `[[45,0],[50,0.005],[55,0.03]]` (포자 형성균은 잘 버팀) | 0.0002 | `antagonist_food_curve` x = 이분해성 C `[[0,0.3],[20,0.6],[60,1]]` | fusarium 0.3, phytophthora 0.3 |
| mycorrhiza | 0.004 | `[[8,0.1],[18,1],[30,1],[38,0.3]]` | 수분 `[[0.2,0.5],[0.5,1],[1.3,0.6]]`, O2 `[[0,0.1],[0.4,1]]` | 0.0003 | `[[36,0],[40,0.01],[45,0.05],[50,0.15]]` (태양열 2~4주에 토착 균근균 0 [S30, 낮음]) | 0.0001 | 숙주 `mycorrhiza_host_curve` x = 뿌리 g/m² `[[0,0.05],[5,0.5],[15,1]]` × `mycorrhiza_p_curve` x = 유효 P g/m² `[[0,1],[25,0.6],[50,0.3],[100,0.1]]` | - |
| fusarium | 0.01 | `[[10,0.1],[20,0.6],[28,1],[32,1],[36,0.5]]` | pH `[[4.5,1],[5.5,1],[6.5,0.6],[7.5,0.4]]` | 0.00002 (반감기 약 4년, 후막포자. 생존 연수는 출처 미확인) | `[[37,0],[40,0.017],[43,0.07],[45,0.09],[50,0.25],[55,0.6]]` (젖은 흙 43 °C 2일 약 99%, 40 °C 8~14일 [S2]) | 0 | 부생 `saprophytic_capacity_index` 0.3 + `host_capacity_index` 0.7 × 감염 지수 | antagonist 0.8 |
| phytophthora | 0.03 | `[[7,0.1],[15,0.6],[20,1],[25,1],[30,0.4],[32,0.1]]` (국내 딸기 분리균 최적 20~25 °C [S24]) | 수분 `[[0.5,0.1],[0.9,0.5],[1.2,1]]`, O2 `[[0,1],[0.3,0.6],[0.5,0.1]]` | 0.0001 (반감기 약 9개월, 난포자) | `[[37,0],[40,0.02],[43,0.08],[45,0.12],[50,0.3]]` (출처 미확인. 같은 난균인 Pythium이 시들음병균만큼 버팀 [S3] → 시들음병균보다 약간만 약하게) | 0 | 부생 0.2 + 숙주 0.8 × 감염 지수 | antagonist 0.8 |

- 곡선에 없는 축(예: 분해균의 pH)은 1이에요. 키를 생략하면 로더가 1로 채우지 않고, 데이터에 `[[0,1],[14,1]]`처럼 명시해요(숨은 기본값 금지).
- 경쟁 계수 α는 **비대칭**이에요. 길항균이 병원균 자리를 크게 차지(0.8)하고, 병원균은 길항균 자리를 적게 차지(0.3)해요.

### 작물 감염 `[soil_disease.<id>]` (fusarium, phytophthora)

| 키 | 의미 | fusarium | phytophthora |
| --- | --- | --- | --- |
| `infection_rate_per_hour` | 압력 1일 때 감염 진행 | 0.004 | 0.01 |
| `infection_env_curve_temp` | x = 토양 온도 | `[[15,0],[22,0.4],[28,1],[32,1],[36,0.5]]` (발병 최고 지온 약 28 °C [S6, S7]) | `[[8,0.1],[15,0.5],[20,1],[25,1],[29,0.4],[32,0]]` (발병 25 °C [S7]) |
| `infection_env_curve_o2` | x = aeration_index | `[[0,1],[1,1]]` | `[[0,1],[0.25,0.6],[0.4,0.05],[0.5,0]]` |
| `infection_env_curve_ph` | x = pH | `[[4.5,1.8],[5.0,1.7],[5.5,1.4],[6.5,1],[7.5,0.8]]` (pH 5가 6.5의 1.6~1.8배 [S8, 수경]) | `[[0,1],[14,1]]` |
| `onset_infection_index` | `DiseaseOnset` 이벤트 기준 (증상이 보이기 시작) | 0.15 | 0.1 |

뿌리 손상 곡선(`[root_damage]` 추가): `fusarium_curve` x = 감염 지수, y = 시간당 손상 `[[0,0],[0.1,0.0005],[0.4,0.003],[0.8,0.01]]`, `phytophthora_curve` `[[0,0],[0.1,0.001],[0.4,0.006],[0.8,0.02]]` (역병은 급성, 시들음병은 서서히).

### 유익균 제품 `[[inoculant]]`

| 키 | 의미 | 예: `bacillus_product` | `trichoderma_product` | `mycorrhiza_product` |
| --- | --- | --- | --- | --- |
| `id`, `name` | 식별자, 표시 이름 | 고초균제 | 트리코더마제 | 균근균제 |
| `index_gain_per_g_per_m2` | 무리 id → 1 g/m²당 지수 증가(포화식) | antagonist 0.02 | antagonist 0.025 | mycorrhiza 0.02 |
| `C_g_per_kg`, `N_g_per_kg` | 담체 유기물 (이분해성으로) | 300, 10 | 300, 10 | 200, 5 |

## 프로세스

### 1. 개체군 갱신 (셀 단계 d2)

모든 무리를 **단계 시작 값 `P`**로 계산하고 마지막에 한꺼번에 바꿔요.

```
suit_g = temp_g(soil_temp) × moisture_g(relative_water) × ph_g(pH) × o2_g(aeration)
K_g    = max(capacity_floor, capacity_g)                   (위 표의 수용력 규칙)
crowd  = (P_g + Σ_h α_gh × P_h) / K_g
growth = r_g × suit_g × P_g × (1 − crowd)                  (crowd > 1이면 음수: 수용력 쪽으로 감소)
kill_natural = base_death_g
kill_heat    = heat_kill_g(soil_temp) × moist_heat_curve(relative_water)
kill_fumi    = fumigant_kill_g × fumigant_residue × fumigant_activation   ([disinfection](disinfection.md))
loss   = P_g × min(1, kill_natural + kill_heat + kill_fumi) + max(0, −growth)
gain   = max(0, growth) + immigration_g × (1 − P_g)
P_g'   = clamp(P_g + gain − min(loss, P_g), 0, 1)
disinfection_kill_g = P_g × min(1, kill_natural + kill_heat + kill_fumi) × (kill_heat + kill_fumi) / (kill_natural + kill_heat + kill_fumi)
                     (분모가 0이면 0. 소독 몫만 따로 기록 → 3번 무기화·망간)
```

- 방향: 적합하면 수용력까지 S자로 늘고, 수용력이 줄면(먹이 고갈, 숙주 없음, 인산 과다) 그쪽으로 줄어요. 소독은 사멸 항으로만 들어와요.
- 기존 시나리오(유익균 1, 병원균 0, 지온 40 °C 미만)에서는 질산화균·분해균이 1 근처에 머물러 **M1~M4 결과가 거의 그대로**예요(1작기 회귀, [m5-overview](m5-overview.md) 10장). 장기간 먹이가 없거나 과습·강산성에서는 내려가요.
- 병원균은 유입이 0이에요. 0인 셀은 계속 0이에요(오염 토양·감염 모종·M5b 잔재로만 생김).

### 2. 다른 프로세스에 주는 활동

| 프로세스 | 바뀌는 식 | 지수 1에서 |
| --- | --- | --- |
| 유기물 분해 (e) | `f = … × decomposer_activity_curve(decomposer_index)` | 1 (M4와 같음) |
| 질산화 1단계 (f) | `n1 = nh4 × min(1, step1_rate × env × step1_ph × aob_activity_curve(aob_index))` | 1 |
| 질산화 2단계 (f) | `n2 = no2 × min(1, step2_rate × env × step2_ph × free_NH3 억제 × nob_activity_curve(nob_index))` | 1 |
| 인산 방출 (f3) | `released = p_fixed × min(1, release_rate × (1 + mycorrhiza_p_release_factor × myco_index))` | 1 + 2 × 지수 |
| 스트레스 (h2)·흡수 (h5) | `water' = water + (1 − water) × water_relief × myco_index`, `salt' = salt + (1 − salt) × salt_relief × myco_index` | 완화 |

- 균근균은 기존 시나리오의 시작값 0.1, 유효 P 25 g/m²에서 수용력 0.6이라 서서히 늘어요. 그래서 기존 작기에서 인산 방출이 조금 늘고 건조·염류가 조금 덜 아파요. **1작기 회귀 허용 오차 안에 드는지가 점검 항목이에요.** 넘으면 시나리오 기본 균근균 지수를 0으로 두는 안을 함께 보고해요.
- 질산화의 환경 곡선(온도·수분·pH)은 그대로 두고, 개체군 적합도는 **얼마나 살아남는가**만 정해요. 같은 곡선이 두 번 곱해져 속도가 이중으로 느려지지 않게, 활동 곡선은 개체군 지수만 입력으로 받아요.

### 3. 작물 감염 (작물 단계 h6, 뿌리 건강도 앞)

작물이 살아 있는 셀에서, 병마다:

```
pressure = pathogen_index × env_temp(soil_temp) × env_o2(aeration) × env_ph(pH)
           × (1 − antagonist_protection_factor × antagonist_index)
infection += infection_rate × pressure × (1 − infection)        (줄지 않음)
if infection ≥ onset_infection_index 처음 → 이벤트 DiseaseOnset{tile, disease}
```

뿌리 건강도 손상 합에 병마다 `<disease>_curve(infection)`을 더해요([strawberry-minimal](strawberry-minimal.md) 5번). 고사 원인(`cause`)에 `fusarium`, `phytophthora`가 추가돼요.

- 숙주 되먹임: 병원균 수용력에 `host_capacity × infection`이 들어가서, 감염된 포기가 흙 속 병원균을 늘려요. 그 흙은 다음 작기(M8)까지 오염이 남아요.
- 감염 지수는 질소 과다 감수성을 받지 않아요(M5b에서 지상부 병에만, 디렉터 결정 요청 8번).

### 4. 유익균 투입 (명령 단계)

`AddInoculant{area, inoculant_id, g_per_m2}`:

```
for 무리 g in 제품의 index_gain:
    P_g += (1 − P_g) × min(1, g_per_m2 × index_gain_g)
담체 C·N → labile_C, labile_N, 장부 C_in_g, N_in_g
```

- 방향: 빈 흙(소독 직후)에 넣으면 많이 늘고, 이미 높으면 조금만 늘어요. 먹이가 없으면 수용력 쪽으로 다시 줄어요.

## 틱 처리 순서 내 위치

셀 단계 d(통기 다음, 분해 앞). d1(약제 잔류)·d3(소독 결과)은 [disinfection](disinfection.md). 감염은 작물 단계 h6의 뿌리 건강도 직전.

## 보존·불변 조건

- 지수 0~1, 감염 지수 0~1이고 줄지 않음, NaN 없음.
- 개체군은 원소 장부 밖이에요(미생물 몸의 원소는 유기물 풀 안에 있다고 봐요). 유익균 투입의 담체만 `C_in`, `N_in`에 들어가요.
- 결과가 무리 처리 순서에 의존하지 않아요(단계 시작 값으로 계산).

## 테스트 (단위)

| 조건 | 기대 |
| --- | --- |
| 지수 1, 적합도 1, 수용력 1 | 거의 변화 없음 (자연 사멸과 성장 균형) |
| 지수 0.05, 적합도 1, 수용력 1 | S자로 증가해 1 근처에 수렴, 넘지 않음 |
| 수용력이 0.3으로 떨어짐 | 0.3 쪽으로 감소 |
| 병원균 0, 유입 0 | 계속 0 |
| 길항균 1 대 0, 같은 병원균 | 길항균 1 쪽 병원균 평형이 낮다 |
| 분해균 1 대 0.2 | 0.2 쪽 분해 C가 `activity` 비율만큼 작다 |
| 2단계균 0.1, 1단계균 1 | NO2- 축적 (식 확인용. 소독 뒤 실제로 이렇게 갈리는지는 디렉터 결정 대기, m5-overview 12장) |
| 균근균 0 대 1, 같은 수분 결손 | 1 쪽 수분 계수가 큼 |
| 유효 P 100 g/m² | 균근균 수용력 ≤ 0.1 |
| 시들음병균 0.5, pH 5.0 대 6.5 | 5.0 쪽 감염이 빠름 |
| 역병균 0.5, 통기 0.1·지온 15 °C 대 통기 0.5 | 과습 쪽만 감염 |
| 감염 지수 증가 | 뿌리 손상 증가, 지수는 줄지 않음 |
| 무리 순서를 바꿔 계산(테스트용) | 같은 결과 |
| 극한: 지온 70 °C, 수분 0, pH 3 | 지수 0~1, NaN 없음 |

## 단순화와 비목표

- 무리 안의 종 차이(Bacillus와 Trichoderma, 1단계의 세균·고세균)는 하나로 묶었어요.
- 미생물 몸의 C·N을 따로 장부에 두지 않아요. 소독 때 풀리는 질소는 난분해성 유기 N에서 꺼내는 근사예요([disinfection](disinfection.md) "무기화 증가").
- 탈질균·망간 산화균은 개체군이 아니에요(곡선 유지).
- 셀 사이 이동 없음. 병원균은 셀마다 따로 늘어요.
- 길항균이 뿌리 활력을 직접 올리는 효과(생장 촉진)는 없어요. "뿌리 보호"는 감염 압력 감소로만 표현해요.
- 균근균의 인산 흡수 경로(균사 흡수)는 고정 인산 방출 증가로 근사했어요.
