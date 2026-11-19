# 지상부 병해충: 탄저병, 잿빛곰팡이병, 흰가루병, 점박이응애·칠레이리응애, 질소 과다 감수성, 병든 포기 제거

- 상태: 초안 (2026-11-19, M5b — M5a 승인 뒤 다듬어 다시 승인받아요. 곡선 값은 아직 자리표시). 2026-11-19 디렉터 결정: 흰가루병 추가, 점적은 설치 장비, 질소 감수성 지표는 조사 후 확정
- 마일스톤: M5b ([m5-overview](m5-overview.md))
- 관련: [house-humidity](house-humidity.md), [soil-microbes](soil-microbes.md)(토양 병원균 틀, 길항균), [strawberry-fruit](strawberry-fruit.md)(과실·낙과 난수), [strawberry-phenology](strawberry-phenology.md)(모종 등급)
- 참고 자료: [references/m5-foliar-humidity.md](../../references/m5-foliar-humidity.md)(2차, 원문 확인), [references/m5-microbes-disease.md](../../references/m5-microbes-disease.md)(1차)

## 목적

지상부 병해충은 **하우스 공기(습도·젖음·온도)와 질소 관리**로 판단이 갈려요.

| 대상 | 좋아하는 조건 | 피해 | 플레이어 대응 |
| --- | --- | --- | --- |
| 탄저병 | 25~30 °C, 젖음, 두상 관수 물 튐, 질소 과다. 감염 모종에 잠복 | 관부 썩음 → 시듦·고사, 과실 반점 | 모종 등급, 점적 설치, 필름 멀칭(물 튐 감소), 발병 포기 제거, 길항균 |
| 잿빛곰팡이병 | 15~25 °C(최적 20~21), 여러 밤에 걸친 누적 잎 젖음, 질소 과다 | 꽃·과실 썩음(수확 손실) | 아침·한낮 환기, 난방, 유동팬, 질소 절제, 병든 과실·포기 제거 |
| 점박이응애 | 25~32 °C(따뜻할수록 빠름). 습도 자체 효과는 약함, 건조하면 천적이 실패 | 잎 광합성 감소 | 습도 유지, 칠레이리응애 방사 |
| 칠레이리응애 (천적) | 17~28 °C, 습도 55% 이상(밑이면 알 부화 실패), 35 °C 이상 성충 못 됨 | - | 방사 시기·양 |
| 흰가루병 | 15~25 °C, 습도 60~98%, **잎의 물방울엔 억제**, 약광·밀식, 질소 과다 | 잎 광합성 저하, 과실 흰가루(품질·판매 불가 과실) | 피복 겹수(빛), 공기 흐름, 질소 절제. 잎을 적시면 줄지만 잿빛곰팡이·탄저가 늘어요 |

## 상태 변수

| 이름 | 위치 | 의미 | 범위 |
| --- | --- | --- | --- |
| `anthracnose_index` | 셀 (soil-microbes 7번 무리) | 흙·잔재의 탄저병 접종원 | 0~1 |
| `infection_anthracnose_index` | 작물 | 관부 감염 | 0~1 (줄지 않음) |
| `botrytis_spore_index` | 하우스 | 공기 중 포자 압력 | 0~1 |
| `infection_botrytis_index` | 작물 | 꽃·과실 감염 압력(포기 단위) | 0~1 |
| `powdery_mildew_spore_index` | 하우스 | 공기 중 포자 압력 | 0~1 |
| `infection_powdery_mildew_index` | 작물 | 잎·과실 감염 정도 | 0~1 (잎이 새로 나면 희석돼 줄 수 있음) |
| `spider_mite_index`, `predator_mite_index` | 작물 | 잎당 밀도(최대 = 1) | 0~1 |

모든 지수는 **플레이어 비공개 진짜 값**이에요(2026-11-19 디렉터 결정). 플레이어는 증상(M7 표현)·검정·작기 리포트로 알아요.

장부 추가: `*_removed_g`(병든 포기 제거로 계 밖으로 나간 원소·건물), `fruit_rot_fresh_g`(썩어 버린 과실), `predators_released`.

## 파라미터 (자리표시, M5a 승인 뒤 참고 범위 B1~B9로 확정)

- `[foliar_disease] n_susceptibility_curve`: x = 질소 지표, y = 감염 배율(1~2, 크기는 출처 없음 → 게임용). **지표: 근권 토양 무기 N(NH4+NO3)의 2주 이동 평균**(2026-11-19 디렉터 결정. 문헌이 공급 쪽 지표를 씀). 작물 상태에 `soil_mineral_N_mean_g_per_m2`(이동 평균, 저장 상태) 추가. 곡선 모양은 과다 쪽만 올리고(1 → 2), N 부족 쪽 U자(토마토 근거)는 넣지 않아요([m5-overview](m5-overview.md) 12장 "질소 과다 분석"): 지금 모델의 체내 N 비율은 겨울에 표준·과다 처방 모두 1.2 상한에 붙어 구분이 안 돼요. 후보 (a) 사치 흡수 상한을 부드럽게 바꾼 체내 N 비율, (b) 토양 무기 N(NH4+NO3) 또는 EC, (c) 최근 2주 N 흡수량 이동 평균.
- 흰가루병: `spore_source`, `spore_decay_rate_per_hour`, `infection_rate_per_hour`, `temp_curve`(15~25 °C), `rh_curve`(60~98%), `free_water_inhibition_factor`, `light_curve`(약광일수록 큼), `density_curve`(LAI), `photosynthesis_damage_curve`, `fruit_curve`.
- 탄저병: `infection_rate_per_hour`, `temp_curve`(기온, 25~30 °C 최대), `wetness_curve`, `splash_transfer_index`(두상 관수 1회가 흙 접종원에서 옮기는 감염), `latent_activation_temp_degC`, `root_damage curve`, `fruit_rot_rate_per_hour`, `mulch_splash_factor`(흑색 필름이 물 튐을 줄임).
- `[[seedling_grade]]`에 `initial_anthracnose_latent_index`(A 0, B 0.02, C 0.08 같은 식), `initial_spider_mite_index`.
- 잿빛곰팡이: `spore_source_per_infection`, `spore_decay_rate_per_hour`, 환기로 빠지는 비율(ach), `infection_rate_per_hour`, `temp_curve`(15~22 °C 최대), `wetness_curve`, `fruit_rot_prob_per_hour`(과실별, biology 난수), 꽃 손실.
- 응애: `growth_rate_per_hour`(하루 r 0.15~0.3 → 시간당 약 0.006~0.012), `temp_curve`, `rh_curve`(50~80% 적온, 약한 효과), `photosynthesis_damage_curve`, 하우스 평균 쪽 이동 `spread_rate_per_hour`.
- 칠레이리응애: r 약 0.19/일(25 °C), `attack_rate`, `handling_time`, `conversion_factor`, `death_rate_per_hour`, `rh_survival_curve`, `temp_survival_curve`, `[[predator]] index_per_individual_per_m2`.

## 프로세스 (방향과 식의 틀)

### 1. 질소 과다 감수성

`s_N = n_susceptibility_curve(지표)`. 탄저병·잿빛곰팡이(·흰가루병) 감염 속도에 곱해요. 토양 병해(시들음병·역병)에는 곱하지 않아요.

### 2. 탄저병

```
latent 활성: 기온 ≥ latent_activation_temp이고 젖음이면 감염이 스스로 진행
infection += rate × temp_curve(house_air) × wetness_curve(w) × s_N
             × (흙 접종원 + infection) × (1 − antagonist_protection × antagonist_index) × (1 − infection)
두상 관수 틱(점적 미설치 하우스의 관수): infection += splash_transfer × anthracnose_index × mulch_splash_factor × (1 − infection)
  (mulch_splash_factor: 필름 없음 1, 세 필름 공통 값 < 1)
흙 접종원: soil-microbes 틀(부생 수용력 + 숙주 = 감염), 열 사멸 있음(태양열 소독으로 줄어듦)
피해: 뿌리 손상 합에 anthracnose_curve(infection)(관부 썩음 → 시듦), 과실 썩음(2번과 같은 난수 방식)
```

### 3. 잿빛곰팡이병

```
spore += Σ셀 (spore_source × infection_botrytis + 썩은 과실 수) / 셀 수 − spore × decay − spore × ach × 비율
infection_botrytis += rate × spore × temp_curve(house_air) × wetness_curve(w) × s_N × (1 − infection)
피해: 개화~수확 전 과실마다 이번 틱 썩을 확률 p = fruit_rot_prob × infection × wetness_curve(w)
      → biology 난수(M4 낙과와 같은 스트림·순서 규칙 뒤에). 썩은 과실은 fruit_rot_fresh_g, 원소는 낙엽 규칙으로 토양에
```

- 방향: 밤새 젖는 날이 이어지면 감염이 쌓이고, 썩은 과실이 포자를 늘려 번져요. 환기·팬·난방이 젖음을 끊어요(S26, S27).

### 4. 점박이응애와 칠레이리응애 (작물 단계 h7)

```
M' = M + r_m × temp_m × rh_m × M × (1 − M) − predation + spread × (하우스 평균 M − M)
predation = attack × M × D / (1 + attack × handling × M)
D' = D + conversion × predation − D × death × (2 − rh_survival − temp_survival)
총생산 × (1 − photosynthesis_damage_curve(M))
```

- `ReleasePredators{area, predator_id, per_m2}`: `D += per_m2 × index_per_individual`, 장부 `predators_released`.
- 작물이 죽거나 제거되면 M, D = 0.

### 5. 흰가루병

```
spore_pm += Σ셀 (spore_source_pm × infection_pm) / 셀 수 − spore_pm × decay − spore_pm × ach × 비율
infection_pm += rate_pm × spore_pm × temp_curve_pm(house_air) × rh_curve_pm(canopy_rh)
                × (1 − free_water_inhibition × w) × light_curve_pm(inside_solar 하루 이동 평균)
                × density_curve_pm(LAI) × s_N × (1 − infection_pm)
희석: 잎이 새로 자란 만큼 infection_pm × (새 잎 / 전체 잎)만큼 줄어요(새 잎은 깨끗함)
피해: 총생산 × (1 − pm_photosynthesis_damage_curve(infection_pm)), 수확 과실 중 흰가루 과실 비율 = pm_fruit_curve(infection_pm) → 기형과처럼 상품 아님(M4 기형과 집계에 원인 표시)
```

- 방향: 습한 공기(잎 사이 습도 높음)를 좋아하지만 **잎이 젖어 있으면(w 큼) 억제**돼요. 그래서 밤새 결로로 젖는 하우스는 잿빛곰팡이가, 젖지 않고 습한·어두운 하우스는 흰가루병이 늘어요(S32). 국내 하우스는 봄(4~5월)과 가을에 많고 한겨울엔 적어요(2차 근거). 약광·밀식 효과는 출처가 없어 게임용 가정이에요.

### 6. 병든 포기 제거 `RemovePlant{area}`

- 셀의 작물을 없애요. 체내 원소·건물은 계 밖(`*_removed_g`)으로 — 보존식에 `− removed` 항이 생겨요.
- 그 셀의 감염 조직이 사라져 잿빛곰팡이·흰가루병 포자 원천과 탄저병 숙주가 없어져요. 흙 접종원은 남아요.
- 빈 셀은 `Plant`(하우스 전체 명령)로는 다시 못 심어요(살아 있는 작물이 있으므로) → 보식 명령은 디렉터 결정(M5b 명세에서).

## 시나리오 테스트

S26~S31([m5-overview](m5-overview.md) 9장 M5b).

## 단순화와 비목표

- 살균제·살비제 없음(필요하면 M7과 함께 검토).
- 포기 사이 전파는 하우스 평균(포자·응애 이동)으로만. 탄저병 물 튐의 이웃 셀 전파는 없음.
- 응애·천적은 밀도 지수, 개체·세대 구조 없음. 휴면 없음.
- 증상 표현(M7): 감염 지수 → 반점·회색 곰팡이·잎 뒷면 거미줄·흰 반점.
