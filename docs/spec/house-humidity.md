# 하우스 습도: 수증기 수지, 결로, 잎 젖음, 공기 흐름, 유동팬, 점적 설치

- 상태: 초안 (2026-11-19, M5b — M5a 승인 뒤 다듬어 다시 승인받아요). 2026-11-19 디렉터 결정: 점적은 설치 장비, 설치 전 관수는 두상 관수
- 마일스톤: M5b ([m5-overview](m5-overview.md))
- 관련: [house-climate](house-climate.md)(창·환기 경로·CO2와 같은 틀), [strawberry-minimal](strawberry-minimal.md)(증산), [soil-water](soil-water.md)(증발), [foliar-disease](foliar-disease.md), [ADR-0011](../decisions/0011-house-climate-in-m4.md) 결정 6
- 참고 자료: [references/m5-foliar-humidity.md](../../references/m5-foliar-humidity.md)(2차), [references/m5-microbes-disease.md](../../references/m5-microbes-disease.md)(1차)

## 목적

M4의 창 판단은 "따뜻함 대 CO2"였어요. M5b는 세 번째 축 **"습기"**를 더해요.

- 겨울에 창을 닫아 두면 작물·흙에서 나온 수증기가 갇혀 밤 평균 85~92%, 새벽엔 95% 이상이 되고 잎이 젖어요 → 잿빛곰팡이병 (국내 딸기 하우스 실측, 2차 근거).
- 아침에 잠깐 열거나 난방으로 기온을 올리면 습도가 내려가요. 유동팬은 하우스 습도는 거의 못 낮추지만(0~3%p) 잎 주변 공기를 움직여 젖은 잎을 빨리 말려요(이 크기는 출처 없음, 게임용).
- 너무 말리면(봄 낮 환기 과다, 습도 55% 미만) 천적 칠레이리응애 알이 부화하지 못해 응애를 못 잡아요.
- 점적 호스를 깔기 전까지 관수는 두상 관수라 잎을 적시고 물을 튀겨 탄저병을 퍼뜨려요(대신 흰가루병은 줄어요). 점적은 돈 내고 까는 장비예요(비용 M7).

## 상태 변수

| 이름 | 위치 | 의미 | 단위 |
| --- | --- | --- | --- |
| `house_vapor_g_per_m3` | 하우스 | 공기 중 수증기 농도(절대 습도) | g/m³ |
| `fan_on` | 하우스 | 유동팬 가동 | bool |
| `drip_installed` | 하우스 | 점적 호스 설치 여부. 설치 후 걷지 않음 | bool |
| `leaf_wetness_index` | 작물 | 잎 젖음 정도 | 0~1 |
| `house_rh_index` (파생) | 하우스 | 상대습도 | 0~1 |
| `airflow_index` (파생) | 하우스 | 잎 주변 공기 흐름 | 0~1 |
| `canopy_rh_index` (파생) | 셀 | 잎 사이 상대습도 (빽빽하고 바람 없으면 높음) | 0~1 |

장부 추가: `fan_hours`(가동 시간, 전기료는 M7), `water_condensed_mm`(결로로 피복을 타고 빠진 물, 진단용 — 토양 물 장부 밖).

## 파라미터 (`[humidity]`, 게임용 임의값, 프로토타입에서 B1~B3 목표로 맞춤)

| 키 | 의미 | 초기값 |
| --- | --- | --- |
| `saturation_vapor_curve` | x = 기온 °C, y = 포화 수증기 농도 g/m³ (도구 `tools/gen_saturation.py`가 Tetens 식으로 생성, 코어는 표만 읽음 — ADR-0004) | `[[-10,2.14],[0,4.85],[5,6.8],[10,9.4],[15,12.8],[20,17.3],[25,23.0],[30,30.4],[35,39.6],[40,51.1],[50,83.0],[70,198]]` |
| `cover_temp_fraction` | 피복 안쪽 면 온도 = 바깥 + 비율 × (하우스 − 바깥). 겹이 많을수록 커요 | 겹수별 `[2겹 0.4, 3겹 0.55, 4겹 0.65]` |
| `condensation_rate_per_hour` | 피복 포화 농도를 넘는 수증기 중 시간당 결로 비율 | 0.5 |
| `vent_airflow_factor` | 환기 경로 1당 공기 흐름 | 1.0 |
| `fan_airflow_index` | 유동팬의 공기 흐름 | 0.5 |
| `canopy_rh_boost_index` | 잎이 빽빽하고(LAI ≥ 기준) 바람이 없을 때 잎 사이 습도 가산 | 0.08 |
| `canopy_lai_ref` | 위 가산이 최대가 되는 LAI | 3.0 |
| `dew_rh_index` | 잎 사이 습도가 이 이상이면 잎이 젖기 시작 | 0.95 |
| `wetting_rate_per_hour` | 젖음 증가(1까지 남은 양의 비율) | 0.5 |
| `drying_rate_per_hour` | 마름 기본 속도 | 0.3 |
| `airflow_drying_factor` | 공기 흐름 1일 때 마름 속도 추가 배율 | 2.0 |
| `drying_rh_curve` | x = 잎 사이 습도, y = 마름 배율 | `[[0.5,1.5],[0.8,1],[0.95,0.2],[1,0]]` |
| `overhead_wet_index` | 두상 관수 직후 잎 젖음 | 1.0 |

## 프로세스

### 1. 공기 흐름 (하우스 단계 2, 환기 경로 계산 직후)

```
airflow = min(1, vent_airflow_factor × path + (fan_on ? fan_airflow_index : 0))
```

### 2. 수증기 수지 (하우스 단계 4, CO2와 함께)

```
source_g   = Σ셀 (transpired_mm + evaporated_mm) × 1000 / 셀 수          (1 mm·m² = 1000 g)
vapor     += source_g / air_volume_m3_per_m2
T_cover    = outside + cover_temp_fraction(cover_layers) × (house_air − outside)
cond       = max(0, vapor − sat(T_cover)) × min(1, condensation_rate)
vapor     −= cond                                    → water_condensed_mm += cond × air_volume / 1000
vapor_out  = outside_rh × sat(outside_air)
vapor     += (vapor_out − vapor) × min(1, ach)       (ach는 CO2와 같은 환기 횟수)
vapor      = min(vapor, sat(house_air))              (넘는 몫은 안개·이슬로 사라짐 — 잎 젖음에는 3번에서 반영)
rh         = vapor / sat(house_air)
```

- 방향: 밤에 바깥이 추우면 피복이 차가워 결로가 수증기를 빼내지만, 하우스 기온도 내려가 상대습도는 90~100%에 머물러요. 낮에 환기하면 바깥 공기(겨울엔 절대 습도가 낮음)로 바뀌어 상대습도가 크게 내려가요. 난방은 하우스 기온을 올려 상대습도를 낮춰요.
- 월드 생성 시 첫 틱 바깥 수증기 농도로 시작해요.

### 3. 잎 젖음 (작물 단계, 병해 앞. 틱 시작 때의 습도를 씀)

```
canopy_rh = min(1, rh + canopy_rh_boost × min(1, LAI / canopy_lai_ref) × (1 − airflow))
if 이번 틱 두상 관수: w = max(w, overhead_wet_index)
elif canopy_rh ≥ dew_rh_index: w += wetting_rate × (1 − w)
else: w −= w × min(1, drying_rate × (1 + airflow_drying_factor × airflow) × drying_rh_curve(canopy_rh))
```

### 4. 명령

- `SetFan{on}`: 다음 틱부터 유동팬 가동/정지. 장부 `fan_hours`.
- `InstallDrip`: 하우스 전체에 점적 호스를 깔아요(`drip_installed = true`, 이미 깔려 있으면 enqueue에서 거부). 장부 `drip_installations`(비용 M7).
- `Irrigate`는 필드가 바뀌지 않아요. **`drip_installed`가 거짓이면 그 관수는 두상 관수**예요: 물 수지는 같고, 3번의 잎 젖음과 [foliar-disease](foliar-disease.md)의 물 튐을 일으켜요.
- 시나리오 `[house] drip_installed`는 schema 7의 **필수 키**예요(숨은 기본값 없음). M1~M5a 시나리오는 지금 결과를 지키려고 `true`로 이행해요(지금까지의 관수는 젖음·물 튐이 없는 점적과 같았어요).

## 보존·불변 조건

- 수증기는 원소·물 장부 밖이에요(증산·증발은 이미 토양 물 장부의 유출). `water_condensed_mm`은 진단용.
- 0 ≤ rh ≤ 1, 0 ≤ w ≤ 1, vapor ≥ 0, NaN 없음.

## 테스트 (단위)

| 조건 | 기대 |
| --- | --- |
| 포화 표 | 단조 증가, 20 °C ≈ 17.3 g/m³ |
| 겨울 밤, 창 닫힘 | rh ≥ 0.9 |
| 같은 밤, 난방 10 °C 대 없음 | 난방 쪽 rh가 낮음 |
| 겨울 한낮, 창 닫힘 대 환기 | 환기 쪽 rh가 낮음 |
| 4겹 대 2겹, 같은 밤 | 4겹 쪽 결로가 작고 rh 높음 |
| 팬 켬 대 끔, rh 0.93 | 켠 쪽 잎이 빨리 마름 |
| LAI 4 대 1, 바람 없음 | 4 쪽 canopy_rh 높음 |
| 두상 관수 | 그 틱 w = 1 |
| 극한: 증산 매우 큼, 바깥 −15 °C | rh ≤ 1, NaN 없음 |

## 단순화와 비목표

- 하우스 공기 한 덩어리, 높이별 습도 차이 없음. 잎 사이 습도는 셀 LAI와 공기 흐름으로만 근사.
- 결로수는 토양으로 떨어지지 않고 빠진다고 봐요(낙수 피해 없음).
- 잎 온도(복사 냉각) 따로 계산하지 않아요. 이슬은 잎 사이 습도 문턱으로 대신해요.
- 증산이 습도에 따라 줄어드는 되먹임(VPD)과 Ca 이동(팁번)은 M7.
- 제습기·포그·차광막 없음.
