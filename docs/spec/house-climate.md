# 하우스 기후: 다중 피복, 측창, 난방, CO2, 저온 피해

- 상태: 승인 (2026-11-18, 프로토타입 점검 결과 반영본 최종 승인)
- 마일스톤: M4
- 관련: [m4-overview](m4-overview.md), [soil-temperature](soil-temperature.md), [strawberry-phenology](strawberry-phenology.md), [ADR-0011](../decisions/0011-house-climate-in-m4.md)

## 목적

하우스 안 기온을 **플레이어가 직접** 관리하게 해요(디렉터 결정 1번).

- 아무것도 안 하면 겨울 밤에는 0 °C 근처나 그 아래로 떨어지고, 9월 한낮에는 30 °C를 훌쩍 넘어요. 저온은 잎·과실을 얼리고, 고온은 화아분화를 막아요.
- 딸기 하우스는 보통 2~3중 피복이에요. **기본은 3중**이고, 피복마다 양쪽 측창이 있어서 **열고 닫을 창이 6개**예요. **휴작기(4~8월 무렵)**에 추가금을 들여 2중·4중으로 바꿀 수 있어요(비용은 M7).
- 피복이 많을수록 밤에 따뜻하지만 빛이 줄어요. 창은 바깥 피복만 열면 효과가 작고, 모든 겹이 뚫려야 바람이 통해요.
- 창은 **0·30·50·70·100% 다섯 단계**로 열어요(2026-11-18 디렉터 결정). 겨울에는 "한 뼘"만 열어 CO2를 살리면서 덜 식힐 수 있어요.
- 난방은 설정 온도를 지키지만 연료를 써요(장부에 기록, 비용은 M7).
- **창을 닫아 두면 따뜻하지만 낮 동안 작물이 CO2를 써서 광합성이 떨어져요.** 열면 CO2는 돌아오지만 추워져요. 퇴비를 많이 넣은 흙은 분해 호흡으로 CO2를 보태요(2026-11-18 디렉터 결정).

## 상태 변수 (하우스)

| 이름 | 의미 | 단위 |
| --- | --- | --- |
| `cover_layers` | 피복 겹수 2~4 (시나리오 시작값 기본 3, 휴작기에 명령으로 변경) | 겹 |
| `window_open_fraction[layer][side]` | 측창 개방률. layer 0 = 가장 바깥, side = left/right. 0, 0.3, 0.5, 0.7, 1.0 중 하나 | 0~1 |
| `heater_on`, `heater_setpoint_degC` | 난방기 켜짐과 설정 온도 | -, °C |
| `house_air_temp_degC` | 하우스 기온 (상태로 승격: 열 관성) | °C |
| `heating_energy_MJ_per_m2` (장부) | 누적 난방 에너지 | MJ/m² |
| `house_co2_ppm` | 하우스 안 CO2 농도 | ppm |
| `outside_co2_ppm` (파생) | 이번 틱 바깥 CO2 농도 (시간대별 표) | ppm |
| `inside_solar_W_per_m2` (파생, 진단) | 이번 틱 하우스 안 일사 = 일사 × 투과율^겹수 | W/m² |
| `heating_power_W_per_m2` (파생, 진단) | 이번 틱 난방 출력 `Q_heat` | W/m² |
| `vent_path_index` (파생, 진단) | 이번 틱 환기 경로 `path` (0~1) | - |

## 파라미터 (`[house]` 재구성, 게임용 임의값)

초안은 3중·창 닫힘·난방 없음에서 M1 하우스 식과 거의 같아지도록 계수를 잡았어요. **프로토타입 점검에서 이 전제를 버렸어요**: 1차 점검에서 겨울 하우스가 너무 추워(수확 0) 일사 이득을 0.063에서 0.18로 올렸고, 그 결과 닫힌 3중은 M1 식보다 한낮에 훨씬 더워요. 그래서 M1~M3 작물·토양 시나리오는 매일 한낮 환기 일정을 갖게 됐어요([m4-overview](m4-overview.md) "시나리오 이행").

| 키 | 의미 | 초기값 | 비고 |
| --- | --- | --- | --- |
| `layer_transmittance_factor` | 피복 한 겹의 일사 투과율 | 0.9 | 3중 0.729 ≈ M1의 0.7 |
| `layer_resistance_m2K_per_W` | 피복 한 겹(+ 공기층)의 단열 저항 (지면 면적 기준) | 0.13 | 3중 U ≈ 2.6 W/m²K |
| `infiltration_W_per_m2_per_K` | 창을 닫아도 새는 열 | 0.5 | |
| `window_bypass_fraction` | 완전히 열린 창 하나가 그 겹의 단열을 없애는 비율 (개방률에 비례) | 0.5 | 양쪽 100%면 그 겹 단열 0 |
| `window_open_levels` | 허용 개방률 | `[0, 0.3, 0.5, 0.7, 1.0]` | 디렉터 결정 |
| `vent_conductance_W_per_m2_per_K` | 모든 겹 양쪽이 열렸을 때 환기 열 교환 | 30 | 맞통풍 |
| `solar_heat_gain_fraction` | 투과 일사 중 기온을 올리는 몫 | 0.18 | 닫힌 3중의 1월 맑은 한낮이 약 20~25 °C가 되게 (2026-11-18 프로토타입 점검으로 조정: 이전 0.063, M1 `solar_gain` 0.015와 맞춘 값) |
| `ground_heat_W_per_m2` | 지중 열 등 기본 열 유입 | 6.1 | M1 `base_offset` 2 °C와 맞춤 |
| `air_relax_rate_per_hour` | 기온이 평형값을 따라가는 시간당 비율 (열 관성) | 0.7 | |
| `heater_capacity_W_per_m2` | 난방기 최대 출력 | 150 | 3중 닫힘에서 바깥보다 약 +45 °C까지 |
| `heater_setpoint_min_degC`, `heater_setpoint_max_degC` | `SetHeater` 설정 온도 허용 범위 | 0, 30 | 게임용 임의값. 2026-11-18 프로토타입 점검으로 신설 (명세에 없던 키, 명령 검증에 필요) |
| `temp_mean_relax_rate_per_hour` | 기온 이동 평균 (strawberry-phenology) | 0.042 | |
| `cover_change_window_doy` | 피복 겹수를 바꿀 수 있는 기간 [시작 day_of_year, 끝 day_of_year] | `[91, 243]` | 4월 1일~8월 31일. 디렉터 결정(휴작기) |
| `min_cover_layers`, `max_cover_layers` | 겹수 범위 | 2, 4 | |

M1의 `house.base_offset_degC`, `solar_gain_degC_per_W_per_m2`, `solar_transmittance_factor`는 삭제해요. 토양 증발(soil-water)과 작물 광합성은 `inside_solar = solar × 투과율^겹수`를 써요.

### CO2 (`[house]`·`[crop]` 추가, 2026-11-18 디렉터 결정)

| 키 | 의미 | 초기값 | 비고 |
| --- | --- | --- | --- |
| `house.outside_co2_ppm_by_hour` | 시각 0~23시의 바깥 CO2 농도 24개 | 아래 식의 정시 값 | 디렉터 지정 식. `tools/gen_co2.py`가 생성 |
| `house.air_volume_m3_per_m2` | 지면 1 m²당 하우스 공기 부피 (평균 높이) | 3.0 | 게임용 임의값 |
| `house.co2_mg_per_m3_per_ppm` | 1 ppm에 해당하는 CO2 질량 농도 | 1.83 | 20 °C, 1기압 (44/22.4 × 273/293) |
| `house.infiltration_air_changes_per_hour` | 창을 닫아도 바뀌는 공기 비율 | 0.3 | 게임용 임의값 |
| `house.vent_air_changes_per_hour` | 모든 겹 양쪽이 열렸을 때 시간당 환기 횟수 | 30 | 게임용 임의값. 맞통풍 경로 `path`를 같이 써요 |
| `house.co2_floor_ppm` | 하우스 CO2 하한 (광합성 보상점 근처) | 50 | 게임용 임의값 |
| `crop.co2_g_per_g_dry_matter` | 건물 1 g을 만드는 데 드는 CO2 | 1.65 | 0.45 g C × 44/12 |
| `crop.co2_growth_curve` | x = 하우스 CO2 ppm, y = 총생산 배율 | `[[50,0],[100,0.4],[200,0.75],[400,1.0],[800,1.2],[1200,1.25]]` | 게임용 임의값. 일반적인 CO2 반응 모양 |

**바깥 CO2 식** (디렉터 지정, t = 시각 [0, 24)):

```
C(t) = 420 + 15·sin(πt/12) + 65.13·exp(−d1(t)² / 3.125) − 21.25·exp(−d2(t)² / 1.28)
d1(t) = ((t + 6.5) mod 24) − 12
d2(t) = ((t − 3) mod 24) − 12
```

- 검증: 최고 500.0 ppm(5시 30분 무렵), 최저 388.0 ppm(15시 무렵). 의도(새벽 피크 약 500, 오후 저점 약 390)와 맞아서 식을 고치지 않았어요.
- 틱이 1시간이라 정시 값 24개만 필요해요. 코어에서 sin·exp를 계산하지 않고, 도구가 만든 표를 데이터에서 읽어요(결정론, ADR-0004). 정시 값: 0시 420.0, 3시 439.4, 5시 494.6, 6시 495.1, 9시 431.9, 12시 420.0, 15시 388.1, 18시 405.0, 21시 409.4.

### 저온 피해 (`[crop]` 추가)

| 키 | 의미 | 초기값 | 비고 |
| --- | --- | --- | --- |
| `cold_leaf_damage_curve` | x = 하우스 기온 °C, y = 시간당 잎 손실 비율 | `[[-6,0.05],[-3,0.015],[-1,0.003],[0,0]]` | 언 잎은 낙엽과 같은 규칙으로 토양에 |
| `cold_fruit_loss_curve` | x = 하우스 기온 °C, y = 시간당 달린 과실·꽃 손실 비율 | `[[-4,0.2],[-2,0.05],[-0.5,0],[0,0]]` | 언 과실은 버림(장부 `*_harvested`가 아니라 낙엽처럼 토양에) |

## 프로세스 (하우스 단계 2, 셀 계산 앞)

### 1. 피복과 창

```
τ      = layer_transmittance_factor ^ cover_layers
a_l    = (open_left_l + open_right_l) / 2                 (겹 l의 개방률, 0~1)
R      = Σ_layer layer_resistance × (1 − window_bypass_fraction × (open_left_l + open_right_l))
U      = min(1 / R, vent_conductance) + infiltration     (R = 0이면 1/R 대신 vent_conductance)
path   = sqrt(n_layers) / sqrt(Σ_layer 1 / a_l²)          (어느 겹이든 a_l = 0이면 path = 0)
         직렬 개구부 근사: 공기가 바깥에서 안까지 모든 겹의 개구부를 차례로 지나요. 모든 겹 a = 1이면 1, 모든 겹 a = x이면 x
         (2026-11-18 단계 개방 도입과 함께 곱셈식 Π(열린 창 수/2)에서 바꿈. 곱셈식은 3겹을 30%씩 열면 0.003으로 사실상 0이었어요)
V      = vent_conductance × path
```

- 방향: 겹이 많을수록 밤에 따뜻하고(U 작음) 어두워요(τ 작음). 바깥 겹만 열면 그 겹의 단열만 줄고 바람은 안 통해요. 모든 겹을 한쪽 30%씩 열면 바람이 약하게(path 0.15), 한쪽 100%면 절반(0.5), 양쪽 100%면 최대로 통해요.

### 2. 평형 기온과 난방

```
G_free = ground_heat + solar × τ × solar_heat_gain_fraction
T_free = outside + G_free / (U + V)
Q_heat = heater_on ? clamp((setpoint − T_free) × (U + V), 0, heater_capacity) : 0
T_eq   = outside + (G_free + Q_heat) / (U + V)
house_air += (T_eq − house_air) × min(1, air_relax_rate)
heating_energy += Q_heat × 0.0036                    (W·h → MJ)
```

- 난방은 설정 온도까지만 열을 넣어요. 창을 열어 둔 채 난방하면 연료가 많이 들고 설정 온도에 못 미칠 수 있어요.
- 월드 생성 시 `house_air`는 첫 틱의 `T_eq`로 시작해요.

### 3. CO2 수지 (셀 계산이 끝난 뒤, 하우스 단계 4)

작물과 흙은 틱 시작 때의 `house_co2_ppm`으로 계산하고, 셀이 끝나면 이번 틱의 흐름으로 농도를 갱신해요.

```
crop_uptake_g  = Σ셀 (총생산 − 유지 호흡) × co2_g_per_g_dry_matter / 셀 수      (음수 = 밤에 내놓음)
soil_release_g = Σ셀 이번 틱 C_respired × 44/12 / 셀 수
mass_per_ppm   = air_volume × co2_mg_per_m3_per_ppm / 1000                         (g/m² per ppm)
co2 += (soil_release_g − crop_uptake_g) / mass_per_ppm
ach  = infiltration_air_changes + vent_air_changes × path
co2 += (outside_co2_ppm[시각] − co2) × min(1, ach)
co2  = max(co2, co2_floor_ppm)
```

- 총생산에 `co2_growth_curve(house_co2_ppm)`를 곱해요([strawberry-fruit](strawberry-fruit.md) 3번).
- 방향: 맑은 겨울 낮에 창을 닫아 두면 몇 시간 안에 200 ppm 아래로 떨어져 광합성이 줄어요. 밤에는 작물·흙 호흡으로 바깥보다 높아져요. 퇴비 분해가 활발한 흙은 낮에도 덜 떨어져요.
- CO2는 보존 장부 밖이에요(광합성 탄소가 장부 밖인 것과 같아요). 하한은 계산 안전장치예요.
- 월드 생성 시 첫 틱 시각의 바깥 값으로 시작해요.

### 4. 저온 피해 (셀 단계 h, 생물계절 앞)

- 잎: `leaf × cold_leaf_damage_curve(house_air)`만큼 낙엽 규칙([strawberry-fruit](strawberry-fruit.md) "낙엽")으로 토양에 가요.
- 과실·꽃: [strawberry-fruit](strawberry-fruit.md)의 과실과 아직 안 핀 화방의 꽃을 `cold_fruit_loss_curve(house_air)` 비율로 잃어요. 잃은 과실의 건물·양분은 낙엽과 같은 규칙으로 토양에 가요.

## 명령 (M4 추가)

| 명령 | 필드 | 효과 |
| --- | --- | --- |
| `SetWindow` | `layer`(0 = 바깥), `side`(left/right), `open_fraction`(0, 0.3, 0.5, 0.7, 1.0) | 다음 틱부터 창 개방률 변경. 상태는 바꿀 때까지 유지. 허용 단계가 아니면 enqueue에서 거부 |
| `SetHeater` | `on`, `setpoint_degC` | 난방기 켜기/끄기와 설정 온도 |
| `SetCoverLayers` | `layers` | 피복 겹수 변경. **휴작기(`cover_change_window_doy`) 안에서만** 받아요. 늘린 겹의 창은 닫힘으로 시작하고, 줄인 겹의 창 상태는 사라져요. 추가금은 M7 |

- `variant` 순서: `Noop, Irrigate, AddCompost, AddFertilizer, AddLime, Till, SetWindow, SetHeater, SetCoverLayers`.
- 검증: `layer < cover_layers`, 설정 온도는 `heater_setpoint_min_degC`~`heater_setpoint_max_degC`(0~30 °C) 안, `SetCoverLayers`는 겹수 범위 안이고 **명령이 적용될 틱의 날짜**가 휴작기 안이어야 해요(밖이면 enqueue에서 거부).
- 시나리오의 `[[schedule]]`로 매일 정해진 시각에 열고 닫을 수 있어요(헤드리스 작기 시나리오가 이렇게 관리해요). 자동 개폐기·온도 연동 제어는 장비(M7)로 남겨요.

## 시나리오 TOML

```toml
[house]
bed_rows = 4
bed_cols = 20
cover_layers = 3          # 2~4, 시작값. 이후 휴작기에 SetCoverLayers로 변경
```

시작 상태는 창 전부 닫힘, 난방 꺼짐이에요.

## 보존·불변 조건

- 열은 장부 밖이에요. 난방 에너지만 누적 기록해요(≥ 0, 줄지 않음).
- 기온은 유한값, 투과율 0~1, U·V ≥ 0.

## 테스트 (단위)

| 조건 | 기대 |
| --- | --- |
| 같은 날씨, 2중 대 4중, 밤 | 4중이 따뜻함 |
| 같은 날씨, 2중 대 4중, 한낮 | 4중의 `inside_solar`가 작음 |
| 3중·창 닫힘·난방 없음, 1월 맑은 한낮 | 기온 약 20~25 °C (2026-11-18 프로토타입 점검: M1 식과 같다는 이전 기대는 버림. 이 테스트의 기대값 변경은 구현 때 디렉터 승인) |
| 맑은 한낮, 창 닫힘 대 모든 겹 양쪽 열림 | 열린 쪽이 많이 시원함 |
| 바깥 겹만 양쪽 열림 대 전부 닫힘 | 차이 작음(바깥 겹 단열만 감소) |
| 모든 겹 한쪽 대 양쪽 | 양쪽이 더 시원함 |
| 모든 겹 한쪽 30% 대 100% | 30% 쪽 환기 경로 0.15, 덜 식고 CO2 회복은 작음 |
| 개방률 0.4로 명령 | enqueue에서 거부 |
| 난방 켜짐, 설정 8 °C, 바깥 −10 °C | 기온 ≈ 8 °C, 에너지 증가 |
| 난방 용량 부족(창 전부 열림) | 기온 < 설정, 출력 = 용량 |
| 하우스 기온 −3 °C | 잎·과실 손실, 손실분이 토양으로 (원소 보존) |
| 바깥 CO2 표 | 24개, 5시·6시가 가장 높고 15시가 가장 낮음 |
| 맑은 낮, 창 닫힘 대 양쪽 열림 | 닫힌 쪽 CO2가 낮고 총생산 배율이 작음 |
| 밤, 창 닫힘 | CO2가 바깥보다 높아짐 |
| 같은 조건, 이분해성 C 많음 | CO2 감소폭이 작음 |
| 작물 흡수가 극단적으로 큼 | CO2 ≥ 하한, NaN 없음 |
| 존재하지 않는 겹의 창 명령 | enqueue에서 거부 |
| 휴작기(6월)에 3 → 4중 | 겹수 4, 새 겹 창 닫힘, 밤 기온 상승·투과율 감소 |
| 작기 중(11월)에 겹수 변경 | enqueue에서 거부 |

## 시나리오 (S18, [m4-overview](m4-overview.md))

- `s18_winter_none`(1월, 창 닫힘·난방 없음) 대 `s18_winter_heated`(난방 6 °C): 무처리 쪽 최저 기온 < 0 °C, 잎 손실이 있고, 난방 쪽 최저 기온 ≥ 약 6 °C, 난방 에너지 > 0.
- `s18_autumn_closed`(9월 10일 시작, 28일, 창 닫힘) 대 `s18_autumn_vented`(매일 8~18시 창 6개 전부 100% 열기): 닫힌 쪽 최고 기온이 30 °C를 넘고 화아분화 진행이 느려요(2026-11-18 프로토타입 점검: 이전 7~14일, 매일 10~16시).
- `s18_co2_closed`(3월 6일(day 65) 시작, 7일, 모종 A × 16포기/m², 활착 1, 창 닫힘·난방 8 °C) 대 `s18_co2_vented`(같은 조건, 매일 8~16시 모든 겹 한쪽 30% 열기): 닫힌 쪽 낮 최저 CO2가 낮고 그날 총생산이 작아요. (2026-11-18 프로토타입 점검: 시작 시점·포기 수·개방 방식을 바꿨고, 초안의 "연 쪽은 기온이 낮고 난방 에너지가 크다"는 조건은 점검 결과에 따라 뺐어요.)

## 단순화와 비목표

- 습도, 결로, 보온 커튼의 낮/밤 개폐(투과율 변화)는 없어요. 습도는 병해(M5)와 함께 봐요.
- **바람 세기(하우스 안 공기 흐름)**는 없어요. 환기는 열·CO2 교환으로만 계산해요. 공기 흐름이 영향을 주는 습도·병해는 **M5**, 증산·Ca 이동(팁번)은 **M7**에서 이어서 다뤄요(2026-11-18 디렉터 결정).
- 지상부 O2는 다루지 않아요. 공기 중 21%로 하우스에서 생장을 제한하지 않아요. 뿌리 O2는 토양 통기(M2)가 맡아요.
- CO2 시용기(장비)는 경제(M7)와 함께 해요.
- 창은 0·30·50·70·100% 다섯 단계예요(연속 개방률은 없어요, 2026-11-18 디렉터 결정).
- 하우스는 하나의 공기 덩어리예요. 겹 사이 공기층 온도는 따로 계산하지 않아요.
- 피복 노후, 바람, 눈 하중은 없어요.
