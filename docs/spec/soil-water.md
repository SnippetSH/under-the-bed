# 토양 수분 수지

- 상태: 승인 (2026-11-18)
- 마일스톤: M1
- 관련: [m1-overview](m1-overview.md), 용탈은 [nitrogen](nitrogen.md)

> **M2 변경 (2026-11-18 승인)**: 포화 수분량 상한(초과분 즉시 배수), 배수 속도 × `drainage_compaction_curve(compaction)`, 관수 시 다짐 증가([aeration](aeration.md)), 작물 증산 항과 토양 증발 × `(1 − 수관 차광)`([strawberry-minimal](strawberry-minimal.md)). 장부에 `water_transpired_mm` 추가.

## 목적

관수량 판단을 만들어요. 물이 모자라면 분해·질산화가 느려지고(이후 작물 흡수도), 넘치면 배수와 함께 질산태 질소가 씻겨 나가요.

## 상태 변수

| 이름 | 의미 | 단위 | 범위 |
| --- | --- | --- | --- |
| `water_mm` | 근권 토양 수분량 | mm | 0 이상 (상한 없음, 포장용수량 초과분은 배수됨) |
| `drained_mm_this_tick` | 이번 틱 배수량. 용탈 단계가 읽어요. 매 틱 시작 시 0 | mm | 0 이상 |
| `field_capacity_mm` (정적) | 포장용수량. 시나리오 `[soil]`에서 와요 | mm | 0 초과 |

파생값: `relative_water = water_mm / field_capacity_mm` (무차원). 다른 프로세스의 수분 계수 입력이에요.

## 파라미터 (`data/params.toml`)

| 키 | 의미 | 단위 | 초기값 | 출처 |
| --- | --- | --- | --- | --- |
| `soil_water.evap_mm_per_hour_per_W_per_m2` | 하우스 안 일사당 잠재 증발 | mm/h per W/m² | 0.0004 | 게임용 임의값 (맑은 날 하루 약 2 mm) |
| `soil_water.evap_base_mm_per_hour` | 일사가 없을 때의 기본 증발 | mm/h | 0.005 | 게임용 임의값 |
| `soil_water.evap_moisture_curve` | x = relative_water, y = 증발 계수 | - | `[[0,0],[0.3,0.15],[0.7,0.8],[1.0,1.0]]` | 게임용 임의값 |
| `soil_water.drainage_rate_per_hour` | 포장용수량 초과분 중 시간당 배수 비율 | 1/h | 0.3 | 게임용 임의값 |
| `house.solar_transmittance_factor` | 하우스 피복 일사 투과율 (soil-temperature와 공유) | - | 0.7 | 게임용 임의값 |

## 프로세스

### 관수 (명령 단계)

`Irrigate`는 대상 셀의 `water_mm`에 `amount_mm`을 그대로 더하고, 장부 `water_in_mm`에 기록해요. M1에서는 관수 시각 지정(점적 등)이 없고 틱 시작 시 한꺼번에 들어가요.

### 증발 (셀 단계 a)

```
inside_solar   = outside_solar × solar_transmittance_factor
potential_mm   = evap_mm_per_hour_per_W_per_m2 × inside_solar + evap_base_mm_per_hour
evaporated_mm  = min(water_mm, potential_mm × evap_moisture_curve(relative_water))
water_mm      -= evaporated_mm
```

- 방향: 일사가 강할수록, 흙이 젖어 있을수록 증발이 커요. 마를수록 증발이 줄어서 0에 수렴하지만 음수가 되지 않아요.
- M1에는 작물이 없어서 증산은 없어요. M2에서 작물 증산 항이 추가돼요.

### 배수 (셀 단계 a, 증발 직후)

```
excess_mm            = max(0, water_mm − field_capacity_mm)
drained_mm           = excess_mm × min(1, drainage_rate_per_hour)
water_mm            -= drained_mm
drained_mm_this_tick = drained_mm
```

- 방향: 포장용수량을 넘은 만큼만, 시간을 두고 빠져요. 한 번에 많이 주면 며칠에 걸쳐 배수와 용탈이 이어져요.
- 배수에 따른 이온 제거(용탈)는 셀 단계 h에서 처리해요([nitrogen](nitrogen.md) "용탈").

## 보존·불변 조건

- 셀 합계 `water_mm = 초기 + water_in_mm − water_evaporated_mm − water_drained_mm` (상대 오차 1e-9)
- `water_mm ≥ 0`, `drained_mm_this_tick ≥ 0`

## 테스트 (단위)

| 조건 | 기대 |
| --- | --- |
| 포장용수량 이하에서 관수 없음 | 배수 0, 수분은 단조 감소 |
| 포장용수량 초과 상태 | 배수 > 0, 초과분이 매 틱 `(1 − drainage_rate)` 비율로 줄어듦 |
| 낮과 밤 | 같은 수분에서 낮 증발 > 밤 증발 |
| 1000 mm 관수 (극한) | NaN 없음, 수분은 결국 포장용수량 근처로 내려옴 |
| 1년 무관수 | 수분 ≥ 0, 0 근처로 수렴 |

## 단순화와 비목표

- 단일층, 수평 이동 없음, 지하수 상승 없음
- 포화 상한과 지표 유출 없음 (통기성과 함께 M2에서 검토)
- 멀칭에 의한 증발 억제는 멀칭 행동이 생길 때 계수로 추가
