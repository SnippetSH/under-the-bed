# 질소: 비료, 2단계 질산화, 용탈, 아질산 독성

- 상태: 승인 (2026-11-18)
- 마일스톤: M1
- 관련: [m1-overview](m1-overview.md), [organic-matter](organic-matter.md), [soil-water](soil-water.md)

## 목적

비료 형태와 토양 pH의 조합 판단(S2), 관수와 질소 손실의 판단(S3)을 만들어요. 총 질소량만 보면 안 되고 **형태**를 봐야 한다는 경험이 핵심이에요.

## 상태 변수 (셀)

| 이름 | 의미 | 단위 |
| --- | --- | --- |
| `nh4_N_g_per_m2` | 암모늄태 질소. 분해 산물, 질산화 원료. 대부분 흙에 붙어 있어서 잘 안 씻김 | g N/m² |
| `no2_N_g_per_m2` | 아질산태 질소. 질산화 중간체, 독성 | g N/m² |
| `no3_N_g_per_m2` | 질산태 질소. 주 흡수 형태, 잘 씻김 | g N/m² |
| `ph` | 토양 pH. **M1에서는 시나리오 초기값으로 고정** | - |
| `nitrite_alarm` | 아질산 임계치 초과 상태 | bool |

## 파라미터

| 키 | 의미 | 단위 | 초기값 | 출처 |
| --- | --- | --- | --- | --- |
| `nitrification.step1_rate_per_hour` | NH4+ → NO2- 최대 속도 | 1/h | 0.01 | 게임용 임의값 |
| `nitrification.step2_rate_per_hour` | NO2- → NO3- 최대 속도 (평소에는 1단계보다 빨라서 NO2-가 안 쌓여요) | 1/h | 0.08 | 게임용 임의값 |
| `nitrification.temp_curve` | x = 토양 온도 °C | - | `[[0,0],[5,0.1],[15,0.5],[25,0.9],[32,1.0],[40,0.6],[50,0.1]]` | 게임용 임의값 |
| `nitrification.moisture_curve` | x = relative_water | - | `[[0,0],[0.3,0.2],[0.6,0.8],[0.9,1.0],[2.0,1.0]]` | 게임용 임의값 (과습 억제는 M2 통기성) |
| `nitrification.step1_ph_curve` | x = pH | - | `[[4.5,0.1],[5.5,0.5],[6.5,1.0],[8.0,1.0],[9.0,0.6]]` | 게임용 임의값 |
| `nitrification.step2_ph_curve` | x = pH. **1단계보다 고pH에서 더 가파르게 감소** | - | `[[4.5,0.1],[5.5,0.5],[6.5,1.0],[7.2,0.9],[8.0,0.35],[9.0,0.1]]` | 게임용 임의값 |
| `nitrification.ammonia_fraction_ph_curve` | x = pH, y = NH4+ 중 유리 암모니아(NH3) 비율 | - | `[[6,0.0006],[7,0.0056],[7.5,0.0175],[8,0.053],[8.5,0.15],[9,0.36],[10,0.85]]` | 헨더슨-하셀바흐 식, pKa 9.25(25 °C)를 구간 선형으로 근사 |
| `nitrification.free_ammonia_inhibition_curve` | x = 유리 암모니아 지수 (g N/m²), y = 2단계 억제 계수 | - | `[[0,1],[0.05,0.9],[0.3,0.4],[1.0,0.1],[3.0,0.05]]` | 게임용 임의값 |
| `nitrification.nitrite_toxicity_threshold_g_N_per_m2` | 아질산 독성 이벤트 임계치 | g N/m² | 2.0 | 게임용 임의값 |
| `nitrification.nitrite_alarm_reset_fraction` | 임계치의 이 비율 아래로 내려가야 경보 해제 (깜빡임 방지) | - | 0.8 | 게임용 임의값 |
| `leaching.nh4_solution_fraction` | NH4+ 중 용액에 있어 배수와 함께 빠질 수 있는 비율 | - | 0.1 | 게임용 임의값 (M3에서 양이온 교환으로 대체) |

### 비료 정의 (`[[fertilizer]]`)

| 키 | 의미 | 단위 |
| --- | --- | --- |
| `id`, `name` | 식별자, 표시 이름 | - |
| `nh4_N_g_per_kg` | 제품 1 kg당 암모늄태 질소 (요소태 포함) | g/kg |
| `no3_N_g_per_kg` | 제품 1 kg당 질산태 질소 | g/kg |

| id | 이름 | NH4-N | NO3-N | 출처 |
| --- | --- | --- | --- | --- |
| `ammonium_sulfate` | 황산암모늄(유안) | 210 | 0 | 제품 일반 성분 21% N |
| `urea` | 요소 | 460 | 0 | 제품 일반 성분 46% N. 요소 가수분해는 M1에서 즉시 NH4+로 단순화 |
| `calcium_nitrate` | 질산칼슘 | 11 | 144 | 제품 일반 성분 15.5% N |

칼륨, 칼슘 등 다른 성분 키는 해당 마일스톤(M3)에서 추가해요. 지금 넣으면 엄격한 로딩에서 오류가 나요.

## 프로세스

### 비료 투입 (명령 단계)

```
nh4_N += g_per_m2 / 1000 × nh4_N_g_per_kg
no3_N += g_per_m2 / 1000 × no3_N_g_per_kg       → 장부 N_in_g
```

### 질산화 (셀 단계 f)

공통 계수: `env = temp_curve(soil_temp) × moisture_curve(relative_water) × O2계수(=1, M2)`

```
# 1단계
n1     = nh4_N × min(1, step1_rate × env × step1_ph_curve(ph))
nh4_N -= n1 ;  no2_N += n1

# 2단계 (1단계 이후의 NH4+로 유리 암모니아를 계산)
free_ammonia = nh4_N × ammonia_fraction_ph_curve(ph)
n2     = no2_N × min(1, step2_rate × env × step2_ph_curve(ph) × free_ammonia_inhibition_curve(free_ammonia))
no2_N -= n2 ;  no3_N += n2
```

방향:

| 조건 | 2단계 억제 | 결과 |
| --- | --- | --- |
| 중성 토양, 적당한 NH4+ | 약함 | NO2-가 거의 안 쌓임 |
| 고pH | pH 곡선 + 유리 암모니아 비율 증가 (이중) | NO2- 축적 |
| 다량의 암모늄 비료 | 유리 암모니아 증가 | NO2- 축적 |
| 저산소 | (M2 통기성에서) | NO2- 축적 |

- 질산화에 따른 토양 산성화(pH 하강)는 pH가 변수가 되는 M2에서 추가해요.

### 용탈 (셀 단계 h)

이번 틱 배수량 `drained`와 배수 후 수분 `water`로 배수 비율을 구해요.

```
f = drained / (water + drained)      # drained = 0이면 f = 0
no3_N −= no3_N × f
no2_N −= no2_N × f
nh4_N −= nh4_N × f × nh4_solution_fraction      → 합계를 장부 N_leached_g에
```

- 방향: 배수가 많을수록, 질산태 비율이 높을수록 손실이 커요. 암모늄은 잘 안 씻겨요.

### 아질산 독성 판정 (셀 단계 i)

```
threshold = nitrite_toxicity_threshold_g_N_per_m2
if not nitrite_alarm and no2_N > threshold:
    이벤트 NitriteThresholdExceeded{tile_index, no2_N_g_per_m2} ; nitrite_alarm = true
if nitrite_alarm and no2_N < threshold × nitrite_alarm_reset_fraction:
    nitrite_alarm = false
```

- M1에는 작물이 없어서 이벤트만 발생해요. M2에서 이 상태가 뿌리 건강도를 깎아요.

## 보존·불변 조건

- 질산화는 질소 형태만 바꿔요. 셀의 무기 N 합은 질산화 전후로 같아요.
- 셀 합계 질소 보존식은 [m1-overview](m1-overview.md) "장부"를 따라요.
- 모든 풀 ≥ 0

## 테스트

### 단위

| 조건 | 기대 |
| --- | --- |
| pH만 다름 (7.0 대 8.0), 같은 NH4+ | 2단계 계수: 8.0 쪽이 작음 |
| NH4+만 다름 | NH4+가 많은 쪽이 2단계 계수 작음 |
| 질산화 단계 전후 | 무기 N 합 동일 |
| 배수 0 | 용탈 0 |
| 배수 > 0 | NO3- 감소 비율 > NH4+ 감소 비율 |
| 임계치 위아래로 오르내림 | 처음 넘을 때만 이벤트, 해제 비율 아래로 내려간 뒤 다시 넘으면 다시 이벤트 |

### 시나리오

S2, S3 (기대 방향은 [m1-overview](m1-overview.md) "시나리오 테스트")

## 단순화와 비목표

- 탈질, 암모니아 휘산 없음 (기체 손실 0, M4)
- 질산화균 개체군 없음 (M5에서 1·2단계 균을 따로 둬요)
- NH4+ 흡착은 고정 비율로만 표현 (M3 양이온 교환에서 대체)
