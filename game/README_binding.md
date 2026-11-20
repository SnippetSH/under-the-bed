# SimWorld 바인딩 안내 (M6a 본구현, `feature/m6a`)

화면(GDScript)을 만드는 사람이 보는 API 표예요. 설계는 `docs/spec/m6-overview.md` 4장과 17.3이고, 이 문서가 실제 구현과 같은 기준이에요.
C++ 원본은 `sim-godot/src/sim_world.hpp`예요.

- 클래스는 `SimWorld` 하나(`RefCounted`)예요. GDScript에서 `SimWorld.new()`로 만들고, 정적 메서드는 `SimWorld.peek_save_version(...)`처럼 불러요. 신호는 없어요(부르는 순서대로만 상태가 바뀌어요). 신호는 GDScript `SimRunner`가 내요.
- 실패할 수 있는 호출은 결과 사전 `{ok: bool, code: String, message: String}`(+ 메서드별 키)을 돌려줘요. `code`는 `InvalidArgument`, `InvalidData`, `UnsupportedVersion`, `Truncated`, `Io` 중 하나(성공이면 `""`), `message`는 코어·로더의 영어 문장 그대로예요. 예외는 경계를 넘지 않아요.
- 월드를 아직 안 읽었으면(`is_loaded() == false`) 결과 사전 메서드는 `InvalidArgument`/`"world not loaded"`, 조회 메서드는 빈 값(빈 사전·빈 배열·0·`""`)을 돌려줘요.
- 칸 번호(`index`, `tile_index`)는 행 우선이에요: `행 = index / 열 수`, `열 = index % 열 수`. 영역 `area`는 `[row, col, rows, cols]`.
- 값 이름과 단위는 코어 필드 이름 그대로예요(`water_mm`, `soil_temp_degC` …). 래퍼는 계산하지 않아요.
- **`get_true_*`는 개발자 패널 전용**이에요. 게임 화면 코드(칸 그림, HUD, 행동 패널)에서 부르지 않아요. `tools/check_true_access.py`(E8)가 `game/scripts/`·`game/scenes/`에서 허용 목록(`game/scripts/dev_panel.gd`, `game/scripts/dev/` 폴더) 밖의 `get_true_…(` 호출을 찾으면 실패해요. 파일 배치를 바꾸면 그 도구의 `ALLOWED_PATHS`를 같이 고쳐요.
- 스냅샷은 `advance`·`enqueue`·`load_*` 뒤 첫 조회 때 한 번 복사해 두고 다음 변경까지 재사용해요. 한 프레임에 접근자를 여러 번 불러도 복사는 한 번이에요.
- 모든 호출은 메인 스레드에서 해요.

## 만들기·진행

| 메서드 | 돌려주는 것 | 설명 |
| --- | --- | --- |
| `load_files(params_path: String, scenario_path: String)` | 결과 사전 + `weather_path`, `schedule_size` | `res://`·`user://`·절대 경로. 기상 CSV는 시나리오 파일 기준 상대 경로로 찾아요(CLI와 같은 규칙). 시나리오 일정표를 래퍼가 들고 있어요. 성공하면 이벤트·기록·일정표 위치를 비우고 일정표 사용을 켜요. 실패하면 이전 월드가 그대로예요 |
| `is_loaded()` | `bool` | 월드가 있는지 |
| `set_schedule_enabled(on: bool)` / `is_schedule_enabled()` | - / `bool` | 끄면 시나리오 일정표를 넣지 않아요("직접 플레이"). 기본 켜짐. 꺼 둔 동안 지나간 항목은 다시 켜도 넣지 않아요 |
| `advance(ticks: int)` | 결과 사전 + `tick` | 틱마다: 일정표의 그 틱 항목 → `step(1)` → 이벤트 모으기(sim-cli와 같은 순서). 일정표 항목이 거부되면 그 틱을 처리하기 전에 멈추고 오류를 돌려줘요(멈춘 곳은 `get_tick()`). 음수면 `InvalidArgument` |
| `get_tick()` | `int` | 지금 틱(처리한 틱 수) |
| `state_hash()` | `String` `"0x%016x"` | CLI `final_state_hash`와 같은 표기. **비싸요**(Release 약 2 ms, Debug 약 30 ms): 매 틱 부르지 말고 개발자 패널에서 필요할 때만 |

## 명령

| 메서드 | 돌려주는 것 | 설명 |
| --- | --- | --- |
| `enqueue(command: Dictionary)` | 결과 사전 + `seq`, `tick` | 다음 틱 시작 때 적용돼요. 키는 시나리오 `[[schedule]]`과 같아요(`tick`·`repeat_*` 빼고). 잘못된 사전·코어가 거부한 값은 전부 `InvalidArgument`이고 큐·기록은 그대로예요 |
| `can_enqueue(command: Dictionary)` | 결과 사전 | `enqueue`와 **같은 판정**만 하고 아무것도 바꾸지 않아요(코어 C2). 행동 단추를 미리 흐리게 할 때 써요. 지금 상태 기준이라 아직 적용되지 않은 대기 명령의 효과는 보지 않아요(예: 정식을 넣은 같은 틱에 또 정식을 물어도 ok) |
| `take_events()` | `Array[Dictionary]` | 마지막으로 가져간 뒤 쌓인 이벤트(발생 순서). 아래 "이벤트" |
| `take_command_log()` | `Array[Dictionary]` | 마지막으로 가져간 뒤 받아들여진 명령. 항목 `{tick, seq, source: "player"/"schedule", command}` |
| `get_command_log()` | 위와 같은 모양 | 처음부터 전부 |
| `static format_schedule_toml(log: Array)` | `String` | 기록 → `[[schedule]]` 글자(실수는 왕복되는 최단 표기). 항목마다 `# seq …, source …` 주석이 붙어요 |
| `static build_replay_scenario(scenario_text: String, log: Array, weather_csv: String)` | 결과 사전 + `text`, `schedule_size` | 원래 시나리오 글자에서 `[[schedule]]` 표를 빼고 기록을 일정표로 붙인 재생 파일 글자. `weather_csv`가 빈 문자열이 아니면 그 경로로 바꿔요(파일을 다른 폴더에 쓸 때 절대 경로를 넣어요). 붙인 뒤 로더로 다시 읽어 항목 수가 기록과 같은지 확인해요. sim-cli가 그대로 읽어요 |

명령 사전 (영역 명령에서 `area`를 빼면 하우스 전체예요. 숫자 자리에 정수를 넣어도 돼요. 정수 자리(`layer`, `layers`, `area`)에 실수, bool 자리에 정수는 안 돼요):

| `action` | 나머지 키 | 대상 |
| --- | --- | --- |
| `irrigate` | `area`, `amount_mm` | 영역 |
| `add_compost` | `area`, `compost`, `kg_per_m2` | 영역 |
| `add_fertilizer` | `area`, `fertilizer`, `g_per_m2` | 영역 |
| `add_lime` | `area`, `lime`, `g_per_m2` | 영역 |
| `till` | `area` | 영역 |
| `set_window` | `layer`(int, 0 = 가장 바깥), `side`(`"left"`/`"right"`), `open_fraction`(`window_open_levels` 중 하나) | 하우스 |
| `set_heater` | `on`(bool), `setpoint_degC` | 하우스 |
| `set_cover_layers` | `layers`(int) | 하우스 (휴작기만) |
| `thin_flowers` | `area`, `per_plant` | 영역 |
| `set_mulch` | `area`, `mulch`(`"none"` = 걷기) | 영역 |
| `apply_disinfectant` | `area`, `disinfectant`, `g_per_m2` | 영역 |
| `add_inoculant` | `area`, `inoculant`, `g_per_m2` | 영역 |
| `plant` | `seedling_grade`, `plants_per_m2` | 하우스 (살아 있는 작물이 하나라도 있으면 거부) |
| `set_fan` | `on`(bool) | 하우스 |
| `install_drip` | 없음 | 하우스 (한 번만) |
| `remove_plant` | `area` | 영역 (작물이 없는 영역은 거부) |
| `release_predators` | `area`, `predator`, `per_m2` | 영역 |

- 기록의 `command`는 정규형이에요: 영역 명령은 `area`를 항상 적고, 숫자 자리는 `float`예요. 기록의 사전을 그대로 `enqueue`에 다시 넣으면 같은 명령이 돼요.
- 거부 메시지 예: `plant is rejected while a live crop exists`, `set_cover_layers is only accepted on day_of_year 91..243 (got 244)`, `remove_plant area has no crop`, `'action': unknown action 'water' (expected …)`.
- 같은 틱 안의 순서: 플레이어가 넣은 명령(넣은 즉시 큐) → 일정표 항목(`advance` 때). 기록은 `seq` 순서예요.

이벤트 (`take_events()`의 사전, 모두 `tick`과 `type`이 있어요):

| `type` | 나머지 키 |
| --- | --- |
| `command_applied` | `command_seq` |
| `fruit_harvested` | `tile_index`, `fruits`, `deformed`, `fresh_g`, `mean_brix`, `mean_firmness` |
| `plant_died` | `tile_index`, `cause` (코어 표 `kRootDamageCauseIds`: `waterlog`, `heat`, `nitrite`, `acid`, `fusarium`, `phytophthora`, `fumigant`, `anthracnose`). 원인은 개발자 패널에만(결정 F4) |
| `nitrite_threshold_exceeded` | `tile_index`, `no2_N_g_per_m2` |
| `disease_onset` | `tile_index`, `disease` (`fusarium`, `phytophthora`) |
| `foliar_disease_onset` | `tile_index`, `disease` (`anthracnose`, `botrytis`, `powdery_mildew`, `spider_mite`) |

이벤트는 `take_events()`를 부를 때까지 쌓여요. 세이브에는 들어가지 않아요(불러온 직후 비어 있어요).

## 보이는 것 (게임 화면용)

| 메서드 | 돌려주는 것 |
| --- | --- |
| `get_time()` | `{tick, elapsed_days, day_of_year, hour, month, day}`. 다음에 처리할 틱(= 지금 화면 시각)의 달력. `month`(1~12)·`day`(1~31)는 윤년 없는 365일 달력(`tools/run_scenarios.py` `MONTH_START_DOY`와 같은 표)으로 래퍼가 `day_of_year`에서 계산해요 |
| `get_grid_size()` | `Vector2i(x = 열 수, y = 행 수)` (기준 20 × 4) |
| `get_pending_command_count()` | `int` (다음 틱에 적용될 명령 수) |
| `get_catalog()` | `{actions, compost, fertilizer, lime, mulch(+"none"), disinfectant, inoculant, predator, seedling_grade}`(전부 `PackedStringArray`, id), `window_open_levels: PackedFloat64Array`, `heater_setpoint_min_degC`, `heater_setpoint_max_degC`, `cover_change_window_doy: PackedInt64Array[2]`, `min_cover_layers`, `max_cover_layers`, `bed_rows`, `bed_cols` |
| `get_tile_visuals(ripening_min_index: float)` | 길이 = 칸 수인 묶음 배열: `crop_present`·`crop_alive`(`PackedByteArray`, 0/1), `leaf_g_per_m2`(`PackedFloat64Array`), `open_flower_count`·`fruit_count`·`ripening_fruit_count`(`PackedInt32Array`), `mulch_id`(`PackedStringArray`, 없으면 `"none"`). `ripening_min_index`는 게임 쪽 표현 문턱(과실 `development_index`가 이 값 이상이면 "익어 가는 과실") |
| `get_house_controls()` | `{cover_layers, window_open_fraction: PackedFloat64Array [겹0 왼, 겹0 오, 겹1 왼, …] (겹 0 = 가장 바깥), windows_open_count, heater_on, heater_setpoint_degC, fan_on, drip_installed}` |
| `get_weather()` | `{outside_air_temp_degC, solar_radiation_W_per_m2, outside_rh_percent}` (지금 틱부터의 한 시간) |
| `get_harvest_totals()` | `{harvested_fruits, harvested_fresh_g, harvested_deformed, harvested_deformed_fresh_g}` (칸별 수확 기록의 합, 처음부터 누적. 세이브 뒤에도 그대로) |

- 뽑은 칸(`remove_plant`)은 `crop_present = 0`이라 정식 전 빈 칸과 같아 보여요(결정 F2).

## 진짜 값 (개발자 패널 전용)

| 메서드 | 돌려주는 것 |
| --- | --- |
| `get_true_house()` | `HouseState` 전부(이름 그대로) + `house_air_temp_degC`, 창은 `window_open_fraction` 평탄 배열 |
| `get_true_cell(index)` | `SoilCell` 전부. `microbe_index`·`disinfection_kill_index`는 무리 id 키 사전, `fumigant_residue_index`는 약제 id 키 사전, `mulch_id` 추가. 범위 밖이면 빈 사전 |
| `get_true_crop(index)` | `Crop` 스칼라 전부 + `fruit_g_per_m2`, `stress_hours`(`water`, `salt`, `N`, `P`, `K`, `Ca`, `Mg`, `root`, `Mn_deficiency`, `Mn_toxicity`), `infection_index`, `disease_onset_reported`, `foliar_onset_reported`, `truss_count`, `open_flower_count`, `fruit_count`, `harvest`(`HarvestRecord`), `nutrient_status`(작물이 있을 때만, CLI와 같은 코어 함수). 범위 밖이면 빈 사전 |
| `get_true_fruits(index)` | `Array[Dictionary]` 과실마다 `Fruit` 필드 전부 |
| `get_true_column(kind, field)` | `PackedFloat64Array` 길이 = 칸 수(행 우선). `kind = "soil"`: `SoilCell` 실수 필드, `mulch_def_index`, `microbe_index.<무리 id>`, `disinfection_kill_index.<무리 id>`, `fumigant_residue_index.<약제 id>`. `kind = "crop"`: `Crop` 실수 필드, `fruit_g_per_m2`, `stress_hours.<요인>`, `infection_index.<병 id>`. 모르는 이름이면 빈 배열 |
| `get_true_ledger()` | `Ledger` 전부 (`disinfectant_applied_g`는 약제 id 키 사전) |

## 세이브

| 메서드 | 돌려주는 것 | 설명 |
| --- | --- | --- |
| `save_bytes()` | `PackedByteArray` | `World::serialize()` 그대로(파라미터·시나리오·기상·동적 상태·대기 명령·난수 상태, 형식 v8) |
| `get_engine_state()` | `Dictionary` | 코어 세이브에 없는 래퍼 상태: `{format: 1, tick, schedule_enabled, next_schedule_index, schedule_toml, log_toml, log_seq, log_source, log_taken}`. 값이 글자·수·배열뿐이라 `JSON.stringify`로 그대로 묶음에 넣을 수 있어요(JSON을 거쳐 정수가 실수가 돼도 받아요) |
| `load_bytes(bytes, engine_state = {})` | 결과 사전 + `tick` | 코어 바이트로 월드를 만들고, `engine_state`가 있으면 일정표·위치·사용 여부·기록을 되살려요. 실패하면 **이전 월드 그대로**예요. 코어 오류: 버전이 다르면 `UnsupportedVersion`, 잘린 파일 `Truncated`, 매직이 다르거나 빈 파일 `InvalidData`. 엔진 상태 오류: `format`이 다르면 `UnsupportedVersion`, 키가 빠지거나 타입·크기·틱이 안 맞으면 `InvalidData`. `engine_state`를 빼면 일정표·기록이 비어요 |
| `static peek_save_version(bytes)` | 결과 사전 + `version: int`, `supported: bool` | 머리글만 읽어요(코어 C1, 전체 역직렬화 없음). 세이브 목록에서 "불러올 수 있는지"와 안내문("세이브 v7, 지금 v8")에 써요. 머리글이 깨졌으면 `ok = false`, `version = -1`(빈 파일·매직이 다름 → `InvalidData`, 버전 중간에 끝남 → `Truncated`). 머리글은 멀쩡하고 본문만 잘린 파일은 여기서는 `ok`이고 `load_bytes`가 `Truncated`로 거부해요 |
| `static core_save_version()` | `int` | 지금 코어의 세이브 형식 버전(8) |

파일 쓰기·읽기(`user://saves/`, 임시 파일 뒤 이름 바꾸기, 묶음 머리글·요약·구역)는 GDScript 몫이에요(명세 7장).

## 사용 예

```gdscript
var w := SimWorld.new()
var r: Dictionary = w.load_files("res://data/params.toml", "res://data/scenarios/m6/direct_play.toml")
if not r["ok"]: push_error(r["message"])
w.set_schedule_enabled(false)                          # 직접 플레이 (이 판은 일정표도 없어요)
var cmd := {"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0}
var can: Dictionary = w.can_enqueue(cmd)               # 단추를 흐리게 할지
var res: Dictionary = w.enqueue(cmd)
if not res["ok"]: print("거부: ", res["message"])      # 알림으로 보여주기
for i in 4: w.advance(1)                               # 한 프레임에 몇 틱 (배속)
var t: Dictionary = w.get_time()                       # t["month"], t["day"], t["hour"]
var v: Dictionary = w.get_tile_visuals(0.8)            # 0.8 = 게임 쪽 문턱
for e in w.take_events(): print(e["type"], " ", e.get("tile_index", -1))
var bundle := {"engine": w.get_engine_state(), "core": Marshalls.raw_to_base64(w.save_bytes())}
var raw := Marshalls.base64_to_raw(bundle["core"])
if SimWorld.peek_save_version(raw)["supported"]:
	var w2 := SimWorld.new()
	w2.load_bytes(raw, bundle["engine"])
	print(w.state_hash() == w2.state_hash())           # true
```

## 프로토타입(`proto/m6-check`) 바인딩과 달라진 점

- 더한 것: `can_enqueue(command)`(C2), `static peek_save_version(bytes)`·`static core_save_version()`(C1), `is_loaded()`, `get_time()`의 `month`·`day`.
- 뺀 것: `static hello()`, `static fp_state()`(프로토타입 확인용. 부동소수점 상태는 맞출 필요가 없다고 확인됐어요, 17.1 U5). 확장이 올라올 때 찍던 `fp_state process=godot at=load …` 줄도 없어요.
- 바뀐 것: `plant_died.cause`는 래퍼의 복사 표 대신 코어 표(C3)를 써요(글자는 같아요). `load_bytes`는 `engine_state.log_source`에 `"player"`/`"schedule"`이 아닌 값이 있으면 `InvalidData`로 거부해요(전에는 `"player"`로 읽었어요).
- 이름·인자·사전 모양이 같은 나머지 메서드는 그대로예요(`load_files`, `advance`, `enqueue`, `take_events`, `take_command_log`, `get_command_log`, `format_schedule_toml`, `build_replay_scenario`, `get_tick`, `get_grid_size`, `get_pending_command_count`, `get_catalog`, `get_tile_visuals`, `get_house_controls`, `get_weather`, `get_harvest_totals`, `get_true_*`, `save_bytes`, `get_engine_state`, `load_bytes`, `state_hash`, `set_schedule_enabled`, `is_schedule_enabled`).

## 검사

엔진 검사는 `ctest --preset godot`에 등록돼 있어요(`tests/GodotTests.cmake`). Godot 실행 파일은 구성 때 `-DGODOT_EXECUTABLE=<경로>` 또는 환경 변수 `GODOT_BIN`으로 주고, 없으면 건너뛰어요(E8 글자 검사만 돌아요).

| ID | ctest 이름 | 스크립트 |
| --- | --- | --- |
| E1 | `engine_E1_extension_loads` | `game/tests/load_check.gd` (엔진 4.7.2, 틱 0 해시 = CLI, 달력 월·일, 세이브 버전) |
| E2 | `engine_E2_matches_cli_every_tick` | `tools/compare_engine_cli.py --suite` + `game/tests/hash_trace.gd` (목록은 `E2_CASES`) |
| E3 | `engine_E3_save_roundtrip` | `game/tests/save_roundtrip.gd` |
| E4 | `engine_E4_command_log_replay` | `game/tests/replay_check.gd` |
| E5 | `engine_E5_bad_saves_rejected` | `game/tests/bad_saves.gd` |
| E6 | `engine_E6_command_conversion` | `game/tests/command_roundtrip.gd` |
| E7 | `engine_E7_batch_size_independent` | `game/tests/batch_independence.gd` |
| E8 | `engine_E8_no_true_value_access_outside_dev_panel` | `tools/check_true_access.py` |
| E9 | `engine_E9_snapshot_matches_cli_csv` | `game/tests/snapshot_vs_csv.gd` |
| E10 | `engine_E10_auto_irrigation_replay` | `game/tests/auto_irrigation.gd` |
| G1 | `engine_G1_game_save_store` | `game/tests/game_save_store.gd` (게임 스크립트의 세이브 묶음·칸·자동 저장·버전 거부·깨진 파일) |
| G2 | `engine_G2_game_runner` | `game/tests/game_runner.gd` (`SimRunner`: 자동 물 주기 E10·H9, 작기 종료, 263일 해시 = sim-cli H6) |
| G3 | `engine_G3_game_screen` | `game/tests/game_screen.gd` (행동 패널 17종·거부 알림·개발자 패널·시작 화면) |

- 검사 스크립트는 `RESULT PASS`/`RESULT FAIL …`을 찍고 종료 코드로 끝나요. `tools/run_engine_check.py`가 그 줄과 함께 엔진의 `SCRIPT ERROR`·`ERROR:`·누수 경고("ObjectDB instances leaked", "resources still in use")도 실패로 봐요. 검사에서 노드를 만들면 끝에 `free()`해야 해요.
- 등록하지 않은 것: `bench_frame.gd`(측정용), `capture_shots.gd`(화면 캡처 도구, 창을 띄워야 해요: `godot --path game --script res://tests/capture_shots.gd -- out=<폴더>`).
