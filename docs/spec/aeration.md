# 통기성과 다짐

- 상태: 승인 (2026-11-18)
- 마일스톤: M2
- 관련: [m2-overview](m2-overview.md), [soil-water](soil-water.md), [strawberry-minimal](strawberry-minimal.md)

## 목적

과습과 다짐이 산소 부족을 거쳐 뿌리를 상하게 하는 경로를 만들어요(S8). 물을 많이 주는 것, 경운을 미루는 것이 눈에 바로 안 보이는 피해로 이어지고, 증상(시듦, 생장 정체)이 건조 피해와 겹쳐서 측정이 필요해져요.

## 상태 변수 (셀)

| 이름 | 의미 | 단위 | 범위 |
| --- | --- | --- | --- |
| `saturation_mm` (정적) | 포화 수분량 (모든 공극이 물로 찬 상태) | mm | > field_capacity_mm |
| `compaction_index` | 다짐 정도. 0 = 막 경운한 흙, 1 = 완전히 다져진 흙 | - | 0~1 |
| `aeration_index` | 산소 가용도. 매 틱 계산 | - | 0~1 |

## 파라미터

| 키 | 의미 | 단위 | 초기값 | 출처 |
| --- | --- | --- | --- | --- |
| `aeration.compaction_cap_curve` | x = compaction_index, y = 통기 지수 상한 | - | `[[0,1.0],[0.5,0.8],[1.0,0.35]]` | 게임용 임의값 |
| `aeration.organic_C_cap_bonus_per_g_C` | 유기 C 1 g/m²당 상한 증가 | 1/(g/m²) | 0.00004 | 게임용 임의값 (유기 C 3,600 g/m²에서 +0.14) |
| `aeration.compaction_per_mm_irrigation` | 관수 1 mm당 다짐 증가 (빗물·관수에 의한 표면 굳음, 가라앉음) | 1/mm | 0.0015 | 게임용 임의값 |
| `aeration.tilled_compaction_index` | 경운 직후 다짐 지수 | - | 0.1 | 게임용 임의값 |
| `aeration.till_root_damage_fraction` | 작물이 있을 때 경운하면 깎이는 뿌리 건강도 비율 | - | 0.5 | 게임용 임의값 |
| `soil_water.drainage_compaction_curve` | x = compaction_index, y = 배수 속도 배율 | - | `[[0,1.0],[0.5,0.6],[1.0,0.1]]` | 게임용 임의값 |

## 프로세스

### 통기 지수 (셀 단계 c)

```
raw  = 1                                                  (water ≤ field_capacity)
     = (saturation − water) / (saturation − field_capacity) (그 위, 0 아래로는 0)
cap  = min(1, compaction_cap_curve(compaction) + organic_C_cap_bonus_per_g_C × 유기 C)
aeration_index = min(raw, cap)
```

- 방향: 포장용수량을 넘어 포화에 가까울수록, 다져질수록 산소가 부족해요. 유기물이 많으면 덜 다져진 것처럼 버텨요.
- 다진 흙은 물이 적어도 상한 때문에 통기가 낮아요. "물은 적당한데 뿌리가 상한다"는 진단 함정이 여기서 생겨요.

### 산소 계수의 사용처

| 프로세스 | 곡선 | 방향 |
| --- | --- | --- |
| 유기물 분해 | `organic_matter.o2_curve` `[[0,0.15],[0.3,0.6],[0.6,1.0],[1,1.0]]` | 저산소에서 느려짐 |
| 질산화 1단계 | `nitrification.step1_o2_curve` `[[0,0.05],[0.3,0.5],[0.6,1.0],[1,1.0]]` | 저산소에서 느려짐 |
| 질산화 2단계 | `nitrification.step2_o2_curve` `[[0,0.0],[0.3,0.2],[0.6,0.8],[0.8,1.0],[1,1.0]]` | **1단계보다 민감** → 과습에서 아질산 축적 |
| 뿌리 손상 | `root_damage.waterlog_curve` (strawberry-minimal) | 저산소에서 뿌리 건강도 감소 |

(곡선 값은 모두 게임용 임의값)

### 다짐 변화

- **관수 (명령 단계)**: `compaction += amount_mm × compaction_per_mm_irrigation`, 1에서 멈춤.
- **경운 `Till` (명령 단계)**: `compaction = tilled_compaction_index`. 작물이 있으면 `root_health × (1 − till_root_damage_fraction)`.
- 방향: 물을 자주 많이 주면 흙이 점점 다져지고, 배수도 느려져서(아래) 과습이 오래가요. 경운은 정식 전에 해야 해요.

### 수분 명세 변경 (soil-water)

- 배수 속도: `drainage_rate_per_hour × drainage_compaction_curve(compaction)`.
- 포화 초과: 증발 직후 `water > saturation`이면 초과분은 그 틱에 전부 배수돼요(`drained_mm_this_tick`에 포함되어 용탈도 일어남).

## 보존·불변 조건

- 통기 지수, 다짐 지수는 항상 0~1.
- 경운·다짐은 물질을 옮기지 않아요. 보존식에 영향 없음.

## 테스트 (단위)

| 조건 | 기대 |
| --- | --- |
| 수분 ≤ 포장용수량, 다짐 0 | 통기 지수 = 상한 (≈1) |
| 수분 → 포화 | 통기 지수 → 0 (단조 감소) |
| 같은 수분, 다짐만 증가 | 통기 지수 감소 또는 같음 |
| 같은 다짐, 유기 C 증가 | 통기 지수 증가 또는 같음 |
| 관수 반복 | 다짐 단조 증가, 1 이하 |
| 경운 | 다짐 = tilled 값, 작물 뿌리 건강도 감소 |
| 다짐 증가 | 같은 초과 수분에서 배수량 감소 |
| 포화 초과 관수 | 초과분이 같은 틱에 배수 |

## 단순화와 비목표

- 토양 공기의 확산·산소 농도를 따로 계산하지 않아요. 통기 지수 하나로 표현해요.
- 다짐은 시간에 따라 저절로 풀리지 않아요(경운으로만 회복). 동결·건조 균열에 의한 회복은 다루지 않아요.
- 사람·장비가 밟아서 생기는 다짐은 없어요.
