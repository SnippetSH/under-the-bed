# SimWorld 바인딩 안내 (M6 프로토타입 P2·P3, `proto/m6-check`)

화면(GDScript)을 만드는 사람이 보는 표예요. 원본 설계는 `docs/spec/m6-overview.md` 4장이고, 다른 점은 맨 아래에 모았어요.

- 클래스는 `SimWorld` 하나(`RefCounted`)예요. 신호는 없어요(부르는 순서대로만 상태가 바뀌어요).
- 실패할 수 있는 호출은 `{ok: bool, code: String, message: String}`을 돌려줘요. `code`는 `InvalidArgument`, `InvalidData`, `UnsupportedVersion`, `Truncated`, `Io` 중 하나, `message`는 코어·로더의 영어 문장 그대로예요.
- 칸 번호(`index`, `tile_index`)는 행 우선이에요: `행 = index / 열 수`, `열 = index % 열 수`. 영역 `area`는 `[row, col, rows, cols]`.
- 값 이름과 단위는 코어 필드 이름 그대로예요(`water_mm`, `soil_temp_degC` …). 래퍼는 계산하지 않아요.
- **`get_true_*`는 개발자 패널 전용**이에요. 게임 화면 코드(칸 그림, HUD, 행동 패널)에서 부르지 않아요(E8 검사 대상).
- 스냅샷은 `advance`·`enqueue`·`load_*` 뒤 첫 조회 때 한 번 복사해 두고 다음 변경까지 재사용해요. 그래서 한 프레임에 접근자를 여러 번 불러도 복사는 한 번이에요.

## 만들기·진행

| 메서드 | 돌려주는 것 | 설명 |
| --- | --- | --- |
| `load_files(params_path, scenario_path)` | 결과 사전 + `weather_path`, `schedule_size` | `res://`·`user://`·절대 경로. 시나리오 일정표를 래퍼가 들고 있어요. 다시 부르면 기록·이벤트도 비워요 |
| `set_schedule_enabled(on: bool)` / `is_schedule_enabled()` | - / `bool` | 끄면 시나리오 일정표를 넣지 않아요("직접 플레이"). 기본 켜짐. 꺼 둔 동안의 항목은 다시 켜도 넣지 않아요 |
| `advance(ticks: int)` | 결과 사전 + `tick` | 틱마다: 일정표의 그 틱 항목 → `step(1)` → 이벤트 모으기. 일정표 항목이 거부되면 멈추고 오류 |
| `get_tick()` | `int` | 지금 틱(처리한 틱 수) |
| `state_hash()` | `String` `"0x%016x"` | CLI와 같은 표기. **비싸요**(릴리스 약 2 ms, 디버그 약 30 ms): 매 틱 부르지 말고 개발자 패널에서 필요할 때만 |
| `static hello()`, `static fp_state()` | | 확인용 |

## 명령

| 메서드 | 돌려주는 것 | 설명 |
| --- | --- | --- |
| `enqueue(command: Dictionary)` | 결과 사전 + `seq`, `tick` | 키는 시나리오 `[[schedule]]`과 같아요(`tick`·`repeat_*` 빼고). 다음 틱 시작 때 적용. 17종 전부 |
| `take_events()` | `Array[Dictionary]` | 마지막으로 가져간 뒤 쌓인 이벤트(발생 순서). 아래 "이벤트" |
| `take_command_log()` | `Array[Dictionary]` | 마지막으로 가져간 뒤 받아들여진 명령. `{tick, seq, source: "player"/"schedule", command}` |
| `get_command_log()` | 위와 같은 모양 | 처음부터 전부 |
| `static format_schedule_toml(log: Array)` | `String` | 기록 → `[[schedule]]` 글자 (실수는 왕복되는 최단 표기) |
| `static build_replay_scenario(scenario_text, log, weather_csv)` | 결과 사전 + `text`, `schedule_size` | 원래 시나리오 글자에서 `[[schedule]]`을 빼고 기록을 붙인 재생 파일 글자. `weather_csv`가 빈 문자열이 아니면 그 경로로 바꿔요(파일을 다른 폴더에 쓸 때 절대 경로를 넣어요). sim-cli가 그대로 읽어요 |

명령 사전 (영역 명령에서 `area`를 빼면 하우스 전체예요. 숫자 자리에 정수를 넣어도 돼요. 정수 자리(`layer`, `layers`, `area`)에 실수는 안 돼요):

| `action` | 나머지 키 |
| --- | --- |
| `irrigate` | `area`, `amount_mm` |
| `add_compost` | `area`, `compost`, `kg_per_m2` |
| `add_fertilizer` | `area`, `fertilizer`, `g_per_m2` |
| `add_lime` | `area`, `lime`, `g_per_m2` |
| `till` | `area` |
| `set_window` | `layer`(int), `side`(`"left"`/`"right"`), `open_fraction` |
| `set_heater` | `on`(bool), `setpoint_degC` |
| `set_cover_layers` | `layers`(int) |
| `thin_flowers` | `area`, `per_plant` |
| `set_mulch` | `area`, `mulch`(`"none"` = 걷기) |
| `apply_disinfectant` | `area`, `disinfectant`, `g_per_m2` |
| `add_inoculant` | `area`, `inoculant`, `g_per_m2` |
| `plant` | `seedling_grade`, `plants_per_m2` |
| `set_fan` | `on`(bool) |
| `install_drip` | 없음 |
| `remove_plant` | `area` |
| `release_predators` | `area`, `predator`, `per_m2` |

- 기록의 `command`는 정규형이에요: 영역 명령은 `area`를 항상 적고, 숫자 자리는 `float`예요.
- 거부되면 `ok = false`이고 큐·기록은 그대로예요. 예: `plant is rejected while a live crop exists`, `unknown key 'foo'`.
- 같은 틱 안의 순서: 플레이어가 넣은 명령(넣은 즉시 큐) → 일정표 항목(`advance` 때). 기록은 `seq` 순서예요.

이벤트 (`take_events()`의 사전, 모두 `tick`과 `type`이 있어요):

| `type` | 나머지 키 |
| --- | --- |
| `command_applied` | `command_seq` |
| `fruit_harvested` | `tile_index`, `fruits`, `deformed`, `fresh_g`, `mean_brix`, `mean_firmness` |
| `plant_died` | `tile_index`, `cause` (`waterlog`, `heat`, `nitrite`, `acid`, `fusarium`, `phytophthora`, `fumigant`, `anthracnose`) |
| `nitrite_threshold_exceeded` | `tile_index`, `no2_N_g_per_m2` |
| `disease_onset` | `tile_index`, `disease` (`fusarium`, `phytophthora`) |
| `foliar_disease_onset` | `tile_index`, `disease` (`anthracnose`, `botrytis`, `powdery_mildew`, `spider_mite`) |

이벤트는 `take_events()`를 부를 때까지 쌓여요. 세이브에는 들어가지 않아요(불러온 직후 비어 있어요).

## 보이는 것 (게임 화면용)

| 메서드 | 돌려주는 것 |
| --- | --- |
| `get_time()` | `{tick, elapsed_days, day_of_year, hour}` |
| `get_grid_size()` | `Vector2i(x = 열 수, y = 행 수)` (기준 20 × 4) |
| `get_pending_command_count()` | `int` (다음 틱에 적용될 명령 수) |
| `get_catalog()` | `{actions, compost, fertilizer, lime, mulch(+"none"), disinfectant, inoculant, predator, seedling_grade}`(전부 `PackedStringArray`, id), `window_open_levels: PackedFloat64Array`, `heater_setpoint_min_degC`, `heater_setpoint_max_degC`, `cover_change_window_doy: PackedInt64Array[2]`, `min_cover_layers`, `max_cover_layers`, `bed_rows`, `bed_cols` |
| `get_tile_visuals(ripening_min_index: float)` | 길이 = 칸 수인 묶음 배열: `crop_present`·`crop_alive`(`PackedByteArray`, 0/1), `leaf_g_per_m2`(`PackedFloat64Array`), `open_flower_count`·`fruit_count`·`ripening_fruit_count`(`PackedInt32Array`), `mulch_id`(`PackedStringArray`, 없으면 `"none"`). `ripening_min_index`는 게임 쪽 표현 문턱(과실 `development_index`가 이 값 이상이면 "익어 가는 과실") |
| `get_house_controls()` | `{cover_layers, window_open_fraction: PackedFloat64Array [겹0 왼, 겹0 오, 겹1 왼, …] (겹 0 = 가장 바깥), windows_open_count, heater_on, heater_setpoint_degC, fan_on, drip_installed}` |
| `get_weather()` | `{outside_air_temp_degC, solar_radiation_W_per_m2, outside_rh_percent}` (지금 틱부터의 한 시간) |
| `get_harvest_totals()` | `{harvested_fruits, harvested_fresh_g, harvested_deformed, harvested_deformed_fresh_g}` (칸별 수확 기록의 합, 처음부터 누적. 세이브 뒤에도 그대로) |

- 뽑은 칸(`remove_plant`)은 `crop_present = 0`이라 정식 전 빈 칸과 같아 보여요(코어 상태에 구분이 없어요).

## 진짜 값 (개발자 패널 전용)

| 메서드 | 돌려주는 것 |
| --- | --- |
| `get_true_house()` | `HouseState` 전부(이름 그대로) + `house_air_temp_degC`, 창은 `window_open_fraction` 평탄 배열 |
| `get_true_cell(index)` | `SoilCell` 전부. `microbe_index`·`disinfection_kill_index`는 무리 id 키 사전, `fumigant_residue_index`는 약제 id 키 사전, `mulch_id` 추가. 범위 밖이면 빈 사전 |
| `get_true_crop(index)` | `Crop` 스칼라 전부 + `fruit_g_per_m2`, `stress_hours`(`water`, `salt`, `N`, `P`, `K`, `Ca`, `Mg`, `root`, `Mn_deficiency`, `Mn_toxicity`), `infection_index`, `disease_onset_reported`, `foliar_onset_reported`, `truss_count`, `open_flower_count`, `fruit_count`, `harvest`(`HarvestRecord`), `nutrient_status`(작물이 있을 때만, CLI와 같은 코어 함수) |
| `get_true_fruits(index)` | `Array[Dictionary]` 과실마다 `Fruit` 필드 전부 |
| `get_true_column(kind, field)` | `PackedFloat64Array` 길이 = 칸 수. `kind = "soil"`: `SoilCell` 실수 필드, `mulch_def_index`, `microbe_index.<무리 id>`, `disinfection_kill_index.<무리 id>`, `fumigant_residue_index.<약제 id>`. `kind = "crop"`: `Crop` 실수 필드, `fruit_g_per_m2`, `stress_hours.<요인>`, `infection_index.<병 id>`. 모르는 이름이면 빈 배열 |
| `get_true_ledger()` | `Ledger` 전부 (`disinfectant_applied_g`는 약제 id 키 사전) |

## 세이브

| 메서드 | 돌려주는 것 | 설명 |
| --- | --- | --- |
| `save_bytes()` | `PackedByteArray` | `World::serialize()` 그대로(파라미터·시나리오·기상·동적 상태·대기 명령·난수 상태, 형식 v8) |
| `get_engine_state()` | `Dictionary` | 코어 세이브에 없는 래퍼 상태: `{format: 1, tick, schedule_enabled, next_schedule_index, schedule_toml, log_toml, log_seq, log_source, log_taken}`. 값이 글자·수·배열뿐이라 `JSON.stringify`로 그대로 묶음에 넣을 수 있어요(JSON을 거쳐 정수가 실수가 돼도 받아요) |
| `load_bytes(bytes, engine_state = {})` | 결과 사전 + `tick` | 코어 바이트로 월드를 만들고, `engine_state`가 있으면 일정표·위치·기록을 되살려요. 실패하면 이전 월드 그대로. `engine_state`를 빼면 일정표·기록이 비어요 |

파일 쓰기·읽기(`user://saves/`, 임시 파일 뒤 이름 바꾸기, 묶음 머리글)는 GDScript 몫이에요(명세 7장).

## 사용 예 (15줄)

```gdscript
var w: Object = ClassDB.instantiate("SimWorld")
var r: Dictionary = w.load_files("res://data/params.toml", "res://data/scenarios/m4/season_standard.toml")
if not r["ok"]: push_error(r["message"])
w.set_schedule_enabled(false)                         # 직접 플레이
var res: Dictionary = w.enqueue({"action": "irrigate", "area": [0, 0, 2, 20], "amount_mm": 4.0})
if not res["ok"]: print("거부: ", res["message"])     # 알림으로 보여주기
for i in 4: w.advance(1)                              # 한 프레임에 몇 틱 (배속)
var v: Dictionary = w.get_tile_visuals(0.8)           # 0.8 = 게임 쪽 문턱
var grid: Vector2i = w.get_grid_size()
for i in v["crop_present"].size(): pass               # 칸 i: v["leaf_g_per_m2"][i] 등으로 그리기
for e in w.take_events(): print(e["type"], " ", e.get("tile_index", -1))  # 이벤트 알림
var bundle := {"engine": w.get_engine_state(), "core": Marshalls.raw_to_base64(w.save_bytes())}
var w2: Object = ClassDB.instantiate("SimWorld")
w2.load_bytes(Marshalls.base64_to_raw(bundle["core"]), bundle["engine"])
print(w.state_hash() == w2.state_hash())              # true
```

## 명세(4장)와 다른 점

- `serialize()` → `save_bytes()`, 엔진 상태는 `get_engine_state()` + `load_bytes(bytes, engine_state)` 두 번째 인자로 주고받아요(명세는 "엔진 쪽 묶음에서 따로 복원"이라고만 했어요).
- 더한 것: `get_command_log()`(전부), `build_replay_scenario()`, `get_harvest_totals()`, `get_tile_visuals`의 문턱 인자, `get_tick()`.
- 아직 없는 것: `can_enqueue`(C2), `peek_save_version`·`core_save_version`(C1). 고사 원인 이름표는 C3 없이 래퍼에 sim-cli와 같은 표를 두었어요.
