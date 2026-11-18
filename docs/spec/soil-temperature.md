# 하우스 기온과 토양 온도

- 상태: 승인 (2026-11-18)
- 마일스톤: M1
- 관련: [m1-overview](m1-overview.md), 분해 발열은 [organic-matter](organic-matter.md)

## 목적

미숙 퇴비의 발열을 드러내는 것이 핵심이에요(S1). 토양 온도는 분해·질산화 속도의 입력이고, M2부터는 뿌리 고온 장해의 원인이 돼요.

## 상태 변수

| 이름 | 의미 | 단위 |
| --- | --- | --- |
| `house_air_temp_degC` (월드) | 하우스 기온. 매 틱 다시 계산하는 파생값이지만 스냅샷에 노출해요 | °C |
| `soil_temp_degC` (셀) | 근권 토양 온도 | °C |

## 파라미터

| 키 | 의미 | 단위 | 초기값 | 출처 |
| --- | --- | --- | --- | --- |
| `house.base_offset_degC` | 외기 대비 기본 온도차 | °C | 2.0 | 게임용 임의값 |
| `house.solar_gain_degC_per_W_per_m2` | 외부 일사당 온실 효과 | °C per W/m² | 0.015 | 게임용 임의값 (맑은 한낮 약 +10 °C) |
| `house.solar_transmittance_factor` | 피복 일사 투과율 (soil-water와 공유) | - | 0.7 | 게임용 임의값 |
| `soil_temperature.relax_rate_per_hour` | 토양 온도가 하우스 기온을 따라가는 시간당 비율 | 1/h | 0.04 | 게임용 임의값 (시간 상수 약 1일) |
| `soil_temperature.heat_degC_per_g_C` | 호흡으로 날아간 탄소 1 g/m²당 토양 온도 상승 | °C per g C/m² | 0.4 | 게임용 임의값 |

## 프로세스

### 하우스 기온 (월드 단계 2)

```
house_air_temp_degC = outside_air_temp_degC + base_offset_degC + solar_gain_degC_per_W_per_m2 × outside_solar
```

- M1 단순화예요. 환기·보온 커튼·난방 명령이 생기면(M7/M8) 이 식에 항이 추가돼요.
- 외기와 일사는 시나리오 기상 표에서 와요.

### 토양 온도 완화 (셀 단계 b)

```
soil_temp_degC += (house_air_temp_degC − soil_temp_degC) × min(1, relax_rate_per_hour)
```

- 방향: 하우스 기온을 일정 지연을 두고 따라가요. 일교차가 기온보다 작게 나타나요.

### 분해 발열 (셀 단계 e, 유기물 분해 직후)

```
soil_temp_degC += respired_C_g_per_m2 × heat_degC_per_g_C
```

- `respired_C_g_per_m2`는 이번 틱에 이 셀에서 호흡으로 날아간 탄소예요([organic-matter](organic-matter.md)).
- 방향: 분해가 빠를수록(미숙 퇴비, 높은 온도) 발열이 커요. 발열 → 온도 상승 → 분해 가속의 양의 되먹임이 있지만, 분해 온도 곡선이 고온에서 꺾이고 이분해성 풀이 소진되면서 멈춰요.
- 발열은 같은 틱의 완화 단계 **뒤에** 더해지므로, 다음 틱 완화에서 하우스 기온 쪽으로 식어요.

## 보존·불변 조건

- 온도는 에너지 보존 대상이 아니에요(게임용 단순화). 대신 NaN·무한대가 없어야 해요.
- 무처리 1년 실행에서 토양 온도는 하우스 기온의 최저~최고 범위 안에 머물러요.

## 테스트 (단위)

| 조건 | 기대 |
| --- | --- |
| 하우스 기온 고정, 토양 온도가 더 낮게 시작 | 단조 증가하며 하우스 기온에 수렴, 넘어서지 않음 |
| 낮 일사 > 0 | 하우스 기온 > 외기 + base_offset |
| 호흡 탄소 x g | 토양 온도가 정확히 `x × heat` 만큼 추가 상승 |

## 단순화와 비목표

- 토양 수분에 따른 열용량 차이 없음 (젖은 흙이 덜 데워지는 효과는 필요해지면 계수로 추가)
- 층별 온도 분포 없음 (2층 토양은 M8)
- 뿌리 고온 장해 판정은 작물이 생기는 M2에서 해요
