# M5a 시나리오 (S19~S25, S33)

`tools/gen_m5a_scenarios.py`가 만들어요 (시드 20261119, 값은 전부 게임용 임의값). 시나리오마다 맨 위 주석에 비교와 기대 방향이 있어요.
기본 소독 틀은 [disinfection 명세](../../../docs/spec/disinfection.md) "시나리오 테스트", 비교는 [m5-overview 9장](../../../docs/spec/m5-overview.md)이에요.
실행: `python tools/run_scenarios.py --cli <sim-cli> --groups M5a-S19 ...` (아래 "실행 일수"가 그룹별 --ticks).

## 실행 일수

| 시나리오 | 그룹 | 일수 | 틱 | 작물 |
| --- | --- | --- | --- | --- |
| `s19_solar_4w` | S19 | 28 | 672 | 작물 없음 |
| `s19_closed_nofilm` | S19 | 28 | 672 | 작물 없음 |
| `s19_vented` | S19 | 28 | 672 | 작물 없음 |
| `s19_solar_2w` | S19 | 28 | 672 | 작물 없음 |
| `s19_solar_dry` | S19 | 28 | 672 | 작물 없음 |
| `s20_none` | S20 | 70 | 1680 | 작물 없음 |
| `s20_solar` | S20 | 70 | 1680 | 작물 없음 |
| `s20_dazomet` | S20 | 70 | 1680 | 작물 없음 |
| `s20_chloropicrin` | S20 | 70 | 1680 | 작물 없음 |
| `s21_plant_day0` | S21 | 98 | 2352 | Plant 명령 |
| `s21_plant_day42` | S21 | 98 | 2352 | Plant 명령 |
| `s21_plant_day0_alkaline` | S21 | 98 | 2352 | Plant 명령 |
| `s21_dazomet_day0` | S21 | 77 | 1848 | Plant 명령 |
| `s21_dazomet_day3` | S21 | 77 | 1848 | Plant 명령 |
| `s21_dazomet_day21` | S21 | 77 | 1848 | Plant 명령 |
| `s21_chloropicrin_day21` | S21 | 77 | 1848 | Plant 명령 |
| `s22_fusarium_none` | S22 | 120 | 2880 | tick 0 [crop] |
| `s22_fusarium_acid` | S22 | 120 | 2880 | tick 0 [crop] |
| `s22_fusarium_antagonist` | S22 | 120 | 2880 | tick 0 [crop] |
| `s22_fusarium_solar` | S22 | 182 | 4368 | Plant 명령 |
| `s23_proper` | S23 | 28 | 672 | tick 0 [crop] |
| `s23_waterlogged_mild` | S23 | 28 | 672 | tick 0 [crop] |
| `s23_waterlogged_hot` | S23 | 28 | 672 | tick 0 [crop] |
| `s23_waterlogged_cold` | S23 | 28 | 672 | tick 0 [crop] |
| `s24_no_compost` | S24 | 60 | 1440 | tick 0 [crop] |
| `s24_compost` | S24 | 60 | 1440 | tick 0 [crop] |
| `s25_standard_none` | S25 | 42 | 1008 | tick 0 [crop] |
| `s25_standard_inoc` | S25 | 42 | 1008 | tick 0 [crop] |
| `s25_dry_none` | S25 | 42 | 1008 | tick 0 [crop] |
| `s25_dry_inoc` | S25 | 42 | 1008 | tick 0 [crop] |
| `s25_highp_none` | S25 | 42 | 1008 | tick 0 [crop] |
| `s25_highp_inoc` | S25 | 42 | 1008 | tick 0 [crop] |
| `s33_disinfect_clear` | S33 | 28 | 672 | 작물 없음 |
| `s33_disinfect_black` | S33 | 28 | 672 | 작물 없음 |
| `s33_disinfect_white` | S33 | 28 | 672 | 작물 없음 |
| `s33_disinfect_none` | S33 | 28 | 672 | 작물 없음 |
| `s33_planted_clear` | S33 | 60 | 1440 | tick 0 [crop] |
| `s33_planted_black` | S33 | 60 | 1440 | tick 0 [crop] |
| `s33_planted_white` | S33 | 60 | 1440 | tick 0 [crop] |
| `s33_planted_none` | S33 | 60 | 1440 | tick 0 [crop] |

## 정한 점 (명세에 정확한 값이 없어서)

- 소독 틱 틀: tick 0 관수 30 mm, tick 1 필름·약제, 소독 끝(태양열 4주 = tick 672, 약제 2주 = tick 336)에 필름을 걷고 창을 전부 열어요. 28일만 돌리면 끝 명령은 실행되지 않아요(S19 기간 비교 때문에 파일에는 넣어 둠). 70일 변형(S20)은 끝 명령 뒤 회복을 봐요.
- 시작 수분은 소독 시나리오에서 30 mm(상대수분 0.5)로 잡아 `관수 없음`(s19_solar_dry)이 마른 흙이 되게 했어요. 관수하면 포장용수량(60 mm) 근처예요.
- 약제 소독(S20·S21)은 가열 영향을 줄이려고 흑색 필름 + 창 열기, 14일 후 필름 걷고 경운 2회(0일, 2일 뒤)예요. 태양열은 투명 필름 + 창 닫음 4주예요. 명세 틀은 "tick 1 clear_film, 창 닫음"이라 약제 쪽만 벗어났어요.
- S21 일수는 정식 후 70일을 기준으로 소독 일수를 더했어요(태양열 28 + 70 = 98일, 약제 7 + 70 = 77일). S21 약제 변형은 필름 7일·경운 1회예요(S20은 14일·경운 2회): 14일 필름 + 경운 2회면 잔류가 정식 전에 다 빠져서 3일 뒤 정식과 21일 뒤 정식의 약제 손상 차이가 안 보였어요. "정식 N일 뒤"는 소독 끝(필름 걷은 날)부터 N일이에요.
- S22 소독 변형은 7/10에 시작해 9/10(tick 1488)에 정식해서 182일이에요. x축을 맞추려면 앞 1488틱을 잘라 비교해요.
- S23은 모든 변형에서 창을 닫아 두고(시작 상태), 지온은 난방으로 맞춰요: 적정·온화 난방 20 도(지온 20~23 도), 저온 난방 12 도(지온 12~13 도), 고온은 난방 없이 9/1 밀폐(지온 첫 2주 30~33 도, 28일째 약 26 도). 난방 없으면 10/21 28일에 지온이 12 도까지, 1/10에는 4 도까지 내려가서 명세의 지온 범위(20~23 / 12 도)를 못 맞춰요. 지온 범위는 첫 실행으로 확인한 값이에요.
- S24 "퇴비 없음"은 이분해성 C·N을 5·0.5 g/m2로 낮춘 흙이에요(길항균 먹이 부족). 길항균·시들음병균 시작값 0.3.
- S33 정식 후는 11/15 시작 60일(11~1월), 활착 1.0, 난방 없음이에요.
