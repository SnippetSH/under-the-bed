# M6 개요: 엔진 연결 (GDExtension 바인딩, 타일 렌더링, 행동 입력, 세이브)

- 상태: 초안 + **디렉터 결정 A1~A9 반영 (2026-11-20, 15장 (0))**. 최종 승인 대기, 코딩 금지
- 날짜: 2026-11-20
- 마일스톤: M6
- 관련 명세: [core-api](core-api.md)(공개 인터페이스, "세이브 버전 호환은 M6에서 다시 다뤄요"), [m5-overview](m5-overview.md)(명령 목록, 진짜 값 비공개), M1~M5 명세 전체(상태·명령의 뜻)
- 관련 ADR: [ADR-0001](../decisions/0001-game-engine.md)(Godot 4 + GDExtension), [ADR-0002](../decisions/0002-data-format.md)(TOML, 파싱은 코어 밖), [ADR-0004](../decisions/0004-determinism-scope.md)(결정론, 코어는 한 번만 빌드), [ADR-0005](../decisions/0005-cpp-toolchain.md), **[ADR-0015](../decisions/0015-godot-version-and-gdextension-build.md)(제안됨: 버전 고정과 빌드 방식)**
- 완료 기준(계획서 7장): **화면에서 1작기 플레이 가능, CLI 결과와 동일한 결과**

M5까지는 숫자와 그래프로만 농사를 지었어요. M6는 **같은 코어를 화면에 붙여서, 플레이어가 마우스로 명령을 넣고 하우스가 자라는 것을 보게** 해요. 시뮬레이션은 한 줄도 바꾸지 않아요. 새 프로세스·새 변수·새 파라미터가 없고, 밸런스도 그대로예요.

## 1. 범위

### 1.1 M6에서 하는 것

| 항목 | 내용 | 장 |
| --- | --- | --- |
| GDExtension 바인딩 | `sim::World`를 Godot에서 쓰는 얇은 래퍼 `sim-godot` | 3, 4 |
| 시간 진행 | 일시정지·배속, 프레임마다 틱을 몇 번 부를지 | 3.4 |
| 타일 렌더링 | 베드 칸·포기·멀칭, 하우스 장치 상태, 날짜·날씨 | 5 |
| 행동 입력 | 코어 명령 17종 전부(`Noop` 제외)를 화면에서 넣기 | 6 |
| 세이브·불러오기 | 코어 바이트 버퍼를 파일로 | 7 |
| 명령 기록·재생 | 플레이 중 넣은 명령을 기록하고, 그대로 다시 돌려 같은 결과 확인 | 7.4, 10 |
| 개발자 패널 | 진짜 값 오버레이, 이벤트 기록 (검증용. 6.4, 질문 A1) | 5.3 |
| 빌드 | godot-cpp, CMake 프리셋, CI | 8 |

### 1.2 M6에서 하지 않는 것 (명시)

| 요소 | M6 처리 | 도입 |
| --- | --- | --- |
| 정보 계층(L0~L3), 측정 행동, 측정 오차, "오래된 정보"·"미지" 표시 | 없음. 스냅샷은 진짜 값뿐이에요(`sim/snapshot.hpp` 주석). 화면의 숫자는 전부 **개발자 패널**이에요 | M7 |
| 증상 화면 표현(황화, 팁번, 시듦, 병반…), 증상 명확도 | 없음. 포기는 크기·생사·꽃·과실만 그려요 | M7 (m5-overview 4장) |
| 경제(자금, 자재비, 난방비, 판매, 시세) | 없음. 행동은 무료·무제한이에요 | M7 |
| 작기 리포트(스트레스 타임라인), 영농일지, 그래프 패널 | 없음. 작기 종료 화면은 CLI가 찍는 합계(수확량·개수·최종 해시)만 | M7 |
| 시나리오 모드 3개, 튜토리얼 | 없음. 시나리오 고르기 목록만 | M7 |
| 자동 개폐기·CO2 시용기, 가스 장해, 세계 변동(ADR-0009) | 없음(코어에 없어요) | M7 |
| 커리어 모드, 작기 이월, 휴작기 흐름 | 없음 | M8 |
| 수동 수확·적엽 행동 | 없음. 수확은 코어가 자동으로 해요(`FruitHarvestedEvent`, strawberry-fruit "수확"). 적엽 명령은 코어에 없어요 | 미정 |
| 최종 아트, 사운드, 현지화, 설정 화면, 키 재설정 | 임시 그림(5.4)·한국어 고정 | M7 이후 |
| 배포판(내보내기) | 질문 A8 (권장: M6 완료 기준에서 제외) | M7 |
| 이전 버전 세이브 변환 | 질문 A6 (권장: 거부 + 안내) | 출시 전 |
| 코어 구조 변경 | 없음. 2.3의 작은 공개 API 추가 3건만 제안 | - |

## 2. 지금 있는 것 (조사 결과)

### 2.1 코어 공개 API (엔진이 쓰는 것, 전부 이미 있어요)

| 필요 | 실제 API | 위치 |
| --- | --- | --- |
| 월드 만들기 | `World::create(Params, Scenario)` → `std::expected<World, Error>` | `sim/world.hpp` |
| 명령 넣기 | `World::enqueue(Command)` → `expected<uint64_t seq, Error>`. 다음 틱 시작 때 투입 순서대로 적용, 잘못된 명령은 거부 | `sim/world.hpp`, `sim/command.hpp` |
| 틱 진행 | `World::step(uint32_t ticks)`. 부를 때마다 `events()`가 비워져요 | `sim/world.hpp` |
| 상태 읽기 | `World::snapshot()` → `sim::Snapshot`(tick, `CalendarTime`, `WeatherHour`, 그리드 크기, 대기 명령 수, `HouseState`, `vector<SoilCell>`, `vector<Crop>`, `Ledger`). **진짜 값만** | `sim/snapshot.hpp`, `sim/soil.hpp`, `sim/house.hpp` |
| 이벤트 | `World::events()` → `span<const Event>` (6종) | `sim/event.hpp` |
| 저장 | `World::serialize()` → `vector<byte>`, `World::deserialize(span<const byte>)`. 파라미터·시나리오·동적 상태·난수 상태 포함, 형식 v8, 버전이 다르면 `UnsupportedVersion` | `sim/world.hpp`, `sim-core/src/serialization.cpp` |
| 일치 검사 | `World::state_hash()` (동적 상태 FNV-1a 64비트) | `sim/world.hpp` |
| 선택지 목록 | `World::params()` → 퇴비·비료·석회·멀칭·약제·유익균·천적·모종 등급 id, `house.window_open_levels`, 난방 설정 범위, `cover_change_window_doy`, 겹수 범위 | `sim/params.hpp` |
| 충족도 등 파생값 | `sim::process::nutrient_status(crop, crop_params)` (CLI가 쓰는 것과 같은 함수) | `sim/processes.hpp` |
| 데이터 읽기 | **문자열 파서** `simdata::parse_params_toml`, `parse_scenario_toml`(→ `ScenarioDocument`: 시나리오 + 기상 CSV 상대 경로 + 일정표), `parse_weather_csv`. 파일 로더 `load_params`, `load_scenario_bundle`(`std::filesystem`) | `simdata/loader.hpp` |
| 실행 방식의 기준 | `sim-cli/src/main.cpp`: 틱 t를 처리하기 직전에 일정표의 `tick == t` 명령을 `enqueue` → `step(1)` → 이벤트 읽기 | `sim-cli/src/main.cpp` |

- 타일 1칸 = 베드 1 m² = 토양 셀 1개 + 작물 0~1개(`Tile`). 작물 하나는 그 칸의 포기 전체(`plants_per_m2`, 기준 8포기)를 대표해요. 기준 그리드는 4 × 20 = 80칸이에요.
- 성능(이 PC, `build/ci` Release, 2026-11-20 측정): `m4/season_standard` 6,312틱이 0.98초 = **틱당 약 0.16 ms**(80칸). 60프레임 기준 프레임당 수십 틱까지 여유가 있어요.

### 2.2 이 PC와 저장소의 Godot

- Godot 4.7.2 안정판(표준 빌드)이 `B:\Tools\Godot`에 있어요(2026-11-20 디렉터 설치, `--version` = `4.7.2.stable.official.ed1daf0bf`, PATH 아님).
- godot-cpp는 저장소에도 이 PC에도 없어요. 저장소에 Godot 프로젝트(`project.godot`, `.gdextension`)도 없어요. `.gitignore`에 Godot 항목(`.godot/`, `*.import`)만 미리 있어요.
- 버전 고정과 빌드 방식은 [ADR-0015](../decisions/0015-godot-version-and-gdextension-build.md)(제안됨)에 따로 적었어요.

### 2.3 M6에 필요한 코어 변경 (제안, 전부 입출력·엔진 코드 없음)

코어에 스냅샷·직렬화·명령이 이미 있어서 **구조 변경은 없어요.** 아래는 공개 API를 조금 여는 것뿐이고, 상태·해시·세이브 형식을 바꾸지 않아요(세이브 v8 그대로, 기존 테스트 기대값 그대로).

| # | 변경 | 이유 | 없으면 |
| --- | --- | --- | --- |
| C1 (필요) | 세이브 형식 버전을 공개: `sim::kSaveFormatVersion`을 공개 헤더로 옮기고, `sim::peek_save_format_version(span<const byte>)` → `expected<uint32_t, Error>` 추가 | 세이브 목록에서 "불러올 수 있는지"를 전체 역직렬화 없이 보여주고, 안내문에 두 버전을 적기 위해서예요. 지금은 상수가 `serialization.cpp`의 이름 없는 네임스페이스 안에 있어요 | 래퍼가 머리글 형식(매직 4바이트 + u32)을 따로 알아야 해서 형식 지식이 두 군데가 돼요 |
| C2 (권장) | `World::can_enqueue(const Command&) const` → `expected<void, Error>`: 지금 private인 `validate_command`를 그대로 공개 | 화면에서 지금 못 하는 행동(휴작기 밖 피복 변경, 작물이 있을 때 정식, 이미 깐 점적)을 **넣기 전에** 흐리게 보여주기 위해서예요 | 눌러 보고 거부 메시지를 받는 방식만 가능(M6는 이걸로도 돌아가요) |
| C3 (권장) | `sim/soil.hpp`에 `kRootDamageCauseIds`(고사 원인 id 배열) 추가. `kSoilDiseaseIds`·`kFoliarDiseaseIds`와 같은 모양 | 고사 원인 이름표가 지금 `sim-cli/src/main.cpp` 안에만 있어요(`kDeathCauseNames`). 래퍼가 또 하나 만들면 두 벌이 돼요 | 래퍼에 같은 표를 복사 |

- 셋 다 core-api 명세 갱신이 따라가요(이 초안 승인 뒤 구현 PR에서). C2·C3를 빼기로 하면 표의 "없으면" 방식으로 가요.
- **코어에 넣지 않는 것**: 파일 읽기·쓰기, 일정표, 배속, 화면용 단계 구분(포기 크기 단계 같은 것), 한국어 이름. 전부 엔진 쪽이에요.

## 3. 구조

### 3.1 층

```
data/*.toml, weather/*.csv ──(Godot FileAccess로 읽은 글자)──┐
                                                           ▼
 Godot 장면 (GDScript)          sim-godot (C++, GDExtension)        sim-data        sim-core
 ┌──────────────────────┐      ┌───────────────────────────┐    ┌───────────┐   ┌───────────┐
 │ SimRunner (시간·기록)  │─명령→│ SimWorld                   │───→│ parse_*   │──→│ World     │
 │ BedView (타일)         │←상태─│  · Dictionary ↔ Command    │    └───────────┘   │  enqueue  │
 │ HUD·행동 패널·세이브    │←이벤트│  · Snapshot → Dictionary   │──────────────────→│  step     │
 │ 개발자 패널            │      │  · bytes ↔ PackedByteArray │                   │  snapshot │
 └──────────────────────┘      └───────────────────────────┘                   └───────────┘
        파일 입출력(user://)          Godot 타입은 여기까지              toml++            표준 라이브러리만
```

- **엔진은 스냅샷을 읽어 그리기만 하고, 플레이어 행동은 명령으로만 보내요** (CLAUDE.md "아키텍처 경계"). 엔진 쪽에는 "물을 주면 수분이 얼마 는다" 같은 계산이 한 줄도 없어요. 화면이 미리 보여주는 예상값도 없어요(M6).
- `sim-core`는 그대로 순수 C++이에요. Godot 타입(`Variant`, `String`, `Dictionary`)은 `sim-godot`에서 끝나요(ADR-0001).
- `sim-godot`은 **변환만** 해요: 값 이름과 단위는 코어 필드 이름 그대로(`water_mm`, `soil_temp_degC`…), 값 계산·반올림·보정 없음. 유일한 파생값은 CLI도 쓰는 `sim::process::nutrient_status`예요.

### 3.2 데이터 파일과 파일 입출력은 어디서

- 코어는 파일을 못 읽어요. `sim-data`의 파일 로더(`load_params` 등)는 `std::filesystem`이라 Godot의 `res://`(내보낸 뒤에는 묶음 파일 안)를 못 읽어요.
- 그래서 **`sim-godot`이 Godot `FileAccess`로 글자를 읽고, `sim-data`의 문자열 파서(`parse_params_toml`, `parse_scenario_toml`, `parse_weather_csv`)에 넘겨요.** 기상 CSV 경로는 `ScenarioDocument::weather_csv`(시나리오 파일 기준 상대 경로)를 래퍼가 풀어요. 파싱 규칙(엄격한 로딩, ADR-0002)은 CLI와 완전히 같은 코드예요.
- 원본은 계속 저장소의 `data/` 하나예요. Godot 프로젝트(`game/`)는 빌드 단계가 `data/`를 `game/data/`로 **복사한 것**을 읽어요(복사본은 커밋하지 않아요. 구현자 판단 B3).
- 세이브 파일 쓰기·읽기는 GDScript(`FileAccess`, `user://`)가 해요. 코어·래퍼는 바이트만 주고받아요.

### 3.3 스레드와 결정론

- 코어 호출은 **전부 Godot 메인 스레드**에서 해요. 작업 스레드를 두지 않아요(v1 틱 처리는 단일 스레드, ADR-0004 규칙 5).
- 결과를 정하는 것은 **"어느 틱 직전에 어떤 명령을 어떤 순서로 넣었는가"뿐**이에요. 프레임 속도, 배속, 일시정지 시간, 화면 크기는 결과에 들어가지 않아요. 실제 시계는 "이번 프레임에 몇 틱 돌릴까"에만 쓰이고, 그건 엔진 쪽 일이에요.
- 그래서 **명령 기록**(7.4) = 그 판의 완전한 재현 수단이에요. 기록의 (틱, 순서, 명령)을 CLI 일정표로 내보내면 CLI가 같은 해시를 내야 해요(10장 E4).
- 래퍼는 `sim-cli`와 같은 순서로 돌려요: 틱 t 직전에 (일정표의 t 항목 → 그 전에 받은 플레이어 명령은 이미 큐에 있음) → `step(1)` → 이벤트 모으기. `step(n)`을 한 번에 부르지 않고 `step(1)`을 n번 불러요(일정표 끼우기와 이벤트 틱 구분 때문에. 결과는 같아요: `step`은 내부에서 한 틱씩 돌아요).
- 같은 틱 안의 순서: 플레이어가 틱 t 상태에서 넣은 명령은 넣은 즉시 `enqueue`해요. 일정표의 t 항목은 `step` 직전에 넣으므로 그 뒤예요. 기록에는 코어가 돌려준 일련번호(`seq`) 순서로 남아서 모호하지 않아요.
- Godot 프로세스 안이라는 차이(부동소수점 제어 상태)는 위험 12장 R2로 다뤄요.

### 3.4 게임 시간과 실제 시간

- 1틱 = 게임 1시간(고정). 배속은 **프레임마다 틱을 여러 번 부르는 방식**이에요(계획서 3장).
- `SimRunner`(GDScript)가 `_process(delta)`에서 `쌓인 시간 += delta × 초당 틱 수`를 하고, 정수 부분만큼 `advance`를 불러요. 한 프레임 상한(틱 수)을 둬서 화면이 멈추지 않게 해요. 상한에 걸리면 배속이 실제로는 느려질 뿐 결과는 같아요.
- 속도 단계(권장, 질문 A2): **일시정지 / 1시간 = 1초 / 6시간 = 1초 / 하루 = 1초 / 열흘 = 1초**. 1작기(263일)는 "하루 = 1초"로 약 4분 반, "열흘 = 1초"로 약 26초예요. 값은 게임 쪽 설정 파일에 둬요(시뮬레이션 파라미터가 아니에요).
- 일시정지 중에도 명령을 넣을 수 있어요. 넣은 명령은 **다음 1시간이 시작될 때** 적용돼요(코어 규칙). 화면은 "대기 중인 명령 N개"(`pending_command_count`)를 보여줘요.
- 큐에 넣은 명령은 **취소할 수 없어요**(코어에 취소가 없어요). 그래서 행동 패널은 "실행" 버튼을 눌러야 넣어요.
- 작기 길이: 코어에는 "작기 끝"이 없어요(CLI는 `--ticks`). 게임 쪽 시나리오 목록 파일이 시나리오마다 길이(틱)를 가져요. 기준 1작기는 `tools/season_regression.py`와 같은 263일 = 6,312틱이에요. 끝나면 자동 일시정지하고 종료 화면을 띄워요(계속 진행도 가능).

## 4. 바인딩 표면 (`sim-godot`)

확장이 Godot에 등록하는 클래스는 **`SimWorld` 하나**(`RefCounted`)예요. 신호는 없어요(끌어오기 방식: 순서가 호출 순서로만 정해지게). 신호는 GDScript `SimRunner`가 내요(4.5).

실패할 수 있는 호출은 모두 `Dictionary { ok: bool, code: String, message: String }`을 돌려줘요. `code`는 `sim::ErrorCode` 이름(`InvalidArgument`, `InvalidData`, `UnsupportedVersion`, `Truncated`, `Io`), `message`는 코어·로더의 메시지 그대로(영어)예요. 예외는 경계를 넘지 않아요(ADR-0005).

### 4.1 메서드

| 메서드 | 코어 대응 | 설명 |
| --- | --- | --- |
| `load_files(params_path, scenario_path) -> Dictionary` | `parse_params_toml` + `parse_scenario_toml` + `parse_weather_csv` + `World::create` | 경로는 `res://`·`user://`·절대 경로. 일정표(`ScenarioDocument::schedule`)를 래퍼가 보관 |
| `set_schedule_enabled(bool)` / `is_schedule_enabled()` | (CLI의 일정표 끼우기) | 끄면 시나리오 일정표를 넣지 않아요("직접 플레이"). 기본 켜짐 |
| `advance(ticks: int) -> Dictionary` | 틱마다 일정표 `enqueue` → `step(1)` | 일정표 항목이 거부되면 CLI처럼 멈추고 오류를 돌려줘요. 이벤트는 내부 버퍼에 쌓여요 |
| `enqueue(command: Dictionary) -> Dictionary` | `World::enqueue` | 성공하면 `seq` 포함. 4.3의 사전 모양 |
| `can_enqueue(command: Dictionary) -> Dictionary` | `World::can_enqueue` (코어 변경 C2) | C2를 빼면 이 메서드도 없어요 |
| `take_events() -> Array[Dictionary]` | `World::events()` | 마지막으로 가져간 뒤 쌓인 이벤트를 발생 순서대로. 4.4 |
| `take_command_log() -> Array[Dictionary]` | - | 마지막으로 가져간 뒤 받아들여진 명령(플레이어 + 일정표): `{tick, seq, source, command}` |
| `get_time() -> Dictionary` | `Snapshot::tick`, `time` | `tick`, `elapsed_days`, `day_of_year`, `hour` |
| `get_grid_size() -> Vector2i` | `bed_cols`, `bed_rows` | x = 열, y = 행 |
| `get_pending_command_count() -> int` | `pending_command_count` | |
| `get_catalog() -> Dictionary` | `World::params()` | 행동 패널 선택지: 종류별 id 목록, 창 개방 단계, 난방 설정 범위, 피복 변경 기간·겹수 범위. 값 계수는 내보내지 않아요 |
| `get_tile_visuals() -> Dictionary` | `Snapshot::crops`, `soil` | 그리기용 묶음 배열(4.2 "보이는 것") |
| `get_house_controls() -> Dictionary` | `Snapshot::house` 중 플레이어가 직접 정한 것 | 겹수, 창 개방률(겹 × 좌우), 난방 켜짐·설정 온도, 팬, 점적 설치 |
| `get_weather() -> Dictionary` | `Snapshot::weather` | 바깥 기온·일사·습도 |
| `get_true_house() -> Dictionary` | `Snapshot::house`, `house_air_temp_degC` | **진짜 값** (개발자 패널 전용) |
| `get_true_cell(index) -> Dictionary` | `SoilCell` | **진짜 값**. 키 = 필드 이름 |
| `get_true_crop(index) -> Dictionary` | `Crop` + `nutrient_status` | **진짜 값**. 화방·과실은 개수 요약, 목록은 `get_true_fruits(index)` |
| `get_true_column(kind, field) -> PackedFloat64Array` | `SoilCell`·`Crop`의 실수 필드 하나 | 오버레이 색칠용, 행 우선 |
| `get_true_ledger() -> Dictionary` | `Ledger` | **진짜 값** |
| `serialize() -> PackedByteArray` | `World::serialize` | |
| `load_bytes(bytes: PackedByteArray) -> Dictionary` | `World::deserialize` | 일정표·기록은 엔진 쪽 묶음(7.2)에서 따로 복원 |
| `static peek_save_version(bytes) -> Dictionary`, `static core_save_version() -> int` | 코어 변경 C1 | |
| `state_hash() -> String` | `World::state_hash` | CLI와 같은 표기 `0x%016x` (64비트 부호 없는 값이라 문자열로) |
| `format_schedule_toml(log: Array) -> String` | - | 명령 기록을 시나리오 `[[schedule]]` 글자로(실수는 왕복되는 최단 표기). CLI 재생용(10장 E4) |

- 이름에 `true`가 붙은 접근자는 **플레이어에게 보여주면 안 되는 진짜 값**이에요. M7이 측정값 접근자(`get_observed_*` 같은 것)를 따로 설계할 수 있게 지금부터 이름으로 갈라 둬요. 게임 화면 코드(`BedView`, HUD, 행동 패널)는 `get_true_*`를 부르지 않고, 개발자 패널만 불러요(검사: 10장 E8).
- 스냅샷은 `advance`·`enqueue`·`load_*` 뒤 첫 조회 때 한 번 복사해 두고 다음 변경까지 재사용해요.
- GDScript의 `float`은 64비트라 값이 깎이지 않아요. 틱·일련번호는 `int`(64비트)예요.

### 4.2 스냅샷 모양

**보이는 것** (`get_tile_visuals()`, 길이 = 칸 수, 행 우선. 게임 화면이 쓰는 유일한 칸별 상태)

| 키 | 타입 | 코어 원천 | 화면 |
| --- | --- | --- | --- |
| `crop_present` | `PackedByteArray` | `Crop::present` | 포기 있음/빈 칸 |
| `crop_alive` | `PackedByteArray` | `Crop::alive` | 살아 있음/죽음 |
| `leaf_g_per_m2` | `PackedFloat64Array` | `Crop::leaf_g_per_m2` | 포기 크기 단계(5.2) |
| `open_flower_count` | `PackedInt32Array` | `Crop::fruits` 중 `set == false` 개수 | 꽃 표시 |
| `fruit_count` | `PackedInt32Array` | `Crop::fruits` 중 `set == true` 개수 | 과실 표시 |
| `ripening_fruit_count` | `PackedInt32Array` | `set`이고 `development_index`가 게임 쪽 문턱 이상인 개수 | 붉은 과실 표시 |
| `mulch_id` | `PackedStringArray` | `SoilCell::mulch_def_index` → `params.mulches[i].id`, 없으면 `"none"` | 멀칭 종류 |

- 이 묶음은 "눈으로 보면 누구나 아는 것"만 담아요: 포기가 있는지, 얼마나 큰지, 꽃·과실이 달렸는지, 무엇을 덮었는지. 색·시듦 같은 **증상은 넣지 않아요**(M7). M7이 L0(육안 관찰)을 정식으로 정의하면 이 표는 그때 다시 승인받아요.
- 하우스 장치(`get_house_controls()`)는 플레이어가 직접 정한 값이라 비밀이 아니에요.
- 바깥 날씨(`get_weather()`)를 플레이어에게 보여줄지는 A1에 묶어 물어요.

**진짜 값** (개발자 패널 전용): `get_true_cell`은 `SoilCell`의 모든 필드(이름 그대로, 배열 필드 `microbe_index`는 `kMicrobeGroupIds`를 키로, `fumigant_residue_index`는 약제 id를 키로), `get_true_crop`은 `Crop`의 스칼라 필드 전부 + `nutrient_status`(충족도·과잉 지수) + `stress_hours`(요인 이름 키) + 화방·과실 개수 + `HarvestRecord`, `get_true_house`는 `HouseState` 전부, `get_true_ledger`는 `Ledger` 전부예요. CLI CSV가 내보내는 값과 같은 원천이에요.

### 4.3 명령 (`sim::Command` 17종)

사전의 키는 **시나리오 `[[schedule]]`과 같아요**(`sim-data/src/loader.cpp` `read_schedule_entry`). 그래서 명령 기록을 그대로 일정표로 쓸 수 있어요. `area`는 `[row, col, rows, cols]`이고 영역 명령에서 빼면 하우스 전체예요.

| `action` | 나머지 키 | `sim::Command` | 대상 |
| --- | --- | --- | --- |
| `irrigate` | `area`, `amount_mm` | `IrrigateCommand` | 영역 |
| `add_compost` | `area`, `compost`, `kg_per_m2` | `AddCompostCommand` | 영역 |
| `add_fertilizer` | `area`, `fertilizer`, `g_per_m2` | `AddFertilizerCommand` | 영역 |
| `add_lime` | `area`, `lime`, `g_per_m2` | `AddLimeCommand` | 영역 |
| `till` | `area` | `TillCommand` | 영역 |
| `set_window` | `layer`, `side`(`left`/`right`), `open_fraction` | `SetWindowCommand` | 하우스 |
| `set_heater` | `on`, `setpoint_degC` | `SetHeaterCommand` | 하우스 |
| `set_cover_layers` | `layers` | `SetCoverLayersCommand` | 하우스 (휴작기만) |
| `thin_flowers` | `area`, `per_plant` | `ThinFlowersCommand` | 영역 |
| `set_mulch` | `area`, `mulch`(`none` = 걷기) | `SetMulchCommand` | 영역 |
| `apply_disinfectant` | `area`, `disinfectant`, `g_per_m2` | `ApplyDisinfectantCommand` | 영역 |
| `add_inoculant` | `area`, `inoculant`, `g_per_m2` | `AddInoculantCommand` | 영역 |
| `plant` | `seedling_grade`, `plants_per_m2` | `PlantCommand` | 하우스 (작물이 없을 때만) |
| `set_fan` | `on` | `SetFanCommand` | 하우스 |
| `install_drip` | - | `InstallDripCommand` | 하우스 (한 번만) |
| `remove_plant` | `area` | `RemovePlantCommand` | 영역 |
| `release_predators` | `area`, `predator`, `per_m2` | `ReleasePredatorsCommand` | 영역 |

- `NoopCommand`는 내보내지 않아요(검증용). 모르는 `action`·모르는 키·빠진 키·타입 불일치는 래퍼가 `InvalidArgument`로 거부해요(로더와 같은 엄격함).
- 검증은 코어가 해요(`World::enqueue`). 래퍼·GDScript는 범위 검사를 다시 구현하지 않아요.

### 4.4 이벤트 (`sim::Event` 6종)

| `type` | 나머지 키 | `sim::EventPayload` | M6 화면 |
| --- | --- | --- | --- |
| `command_applied` | `tick`, `command_seq` | `CommandAppliedEvent` | 대기 표시 지우기 |
| `fruit_harvested` | `tick`, `tile_index`, `fruits`, `deformed`, `fresh_g`, `mean_brix`, `mean_firmness` | `FruitHarvestedEvent` | 칸 위 "수확" 표시, 누적 개수·무게(HUD). 당도·경도는 개발자 패널만 |
| `plant_died` | `tick`, `tile_index`, `cause` | `PlantDiedEvent` | 포기 그림이 죽은 그림으로. 원인(`cause`)은 개발자 패널만 |
| `nitrite_threshold_exceeded` | `tick`, `tile_index`, `no2_N_g_per_m2` | `NitriteThresholdExceededEvent` | 개발자 패널만 |
| `disease_onset` | `tick`, `tile_index`, `disease` | `DiseaseOnsetEvent` | 개발자 패널만 (증상 표현은 M7) |
| `foliar_disease_onset` | `tick`, `tile_index`, `disease` | `FoliarDiseaseOnsetEvent` | 개발자 패널만 |

- `tile_index`는 행 우선이에요(`행 = index ÷ bed_cols`, `열 = index % bed_cols`).
- 이벤트는 세이브에 들어가지 않아요(코어 규칙). 불러온 직후에는 비어 있어요.

### 4.5 GDScript 쪽 (`SimRunner`, 자동 로드 노드)

`SimWorld` 하나를 들고 시간을 굴려요. 신호: `ticked(tick)`(한 프레임의 `advance`가 끝난 뒤 한 번), `events_received(events)`, `command_accepted(seq, command)`, `command_rejected(command, code, message)`, `season_ended()`, `loaded()`. 화면 노드는 이 신호를 받고 `SimWorld` 접근자로 상태를 읽어요.

## 5. 렌더링

### 5.1 화면 구성

- 2D 탑다운. 베드 그리드(`bed_rows × bed_cols`)를 `TileMapLayer` 여러 장으로 그려요(Godot 4.3부터 `TileMap`은 폐기 예정이고 `TileMapLayer`를 써요): 흙·멀칭 층, 포기 층, 선택 영역 층, 개발자 오버레이 층.
- 카메라: 끌어서 이동, 휠 확대·축소. 4 × 20 그리드가 기본 창에 다 들어와요.
- HUD: 날짜(월·일, 정식 뒤 며칠째)·시각, 배속 단추, 대기 명령 수, 누적 수확(개수·무게), 하우스 장치 요약(겹수, 열린 창 수, 난방·팬·점적).
- 하우스 패널: 피복 겹마다 좌·우 창의 개방 단계, 난방기, 유동팬, 점적. 공간 그림이 아니라 단추 판이에요.
- 낮·밤 색조(시각에 따른 어둡기)는 꾸밈이에요(상태를 드러내지 않는 범위에서, 구현자 판단).

### 5.2 M6에서 그리는 상태

| 그리는 것 | 원천 | 방법 |
| --- | --- | --- |
| 빈 칸 / 포기 / 죽은 포기 | `crop_present`, `crop_alive` | 그림 3종 |
| 포기 크기 | `leaf_g_per_m2` | 게임 쪽 문턱으로 3~4단계 그림 |
| 꽃, 과실, 익어 가는 과실 | `open_flower_count`, `fruit_count`, `ripening_fruit_count` | 있음/없음 표시(작은 점). 개수는 그리지 않아요 |
| 멀칭 | `mulch_id` | 종류별 색(투명·흑색·백색 필름) |
| 수확 순간 | `fruit_harvested` 이벤트 | 칸 위에 잠깐 표시 |
| 하우스 장치 | `get_house_controls()` | 하우스 패널·HUD |
| 선택 영역, 대기 중인 명령 | 입력 상태, `pending_command_count` | 테두리, HUD 숫자 |

- **크기 단계 문턱, 익은 과실 문턱은 "표현 규칙"이라 게임 쪽 설정 파일에 둬요**(`data/`가 아니에요: 시뮬레이션 결과에 들어가지 않아요). 전부 "게임용 임의값"으로 표시해요.
- 한 칸은 포기 8개(기준 밀도)를 대표하지만 상태는 칸 단위예요. 한 칸에 무엇을 그릴지는 질문 A7이에요.

### 5.3 개발자 패널 (진짜 값, 검증용)

- **오버레이**: 칸별 값 하나를 골라 색으로 칠해요(`get_true_column`): 수분, 지온, pH, EC, 무기 N 3형태, 통기 지수, 미생물 지수, 감염 지수, 응애 등. 눈금과 최소·최대를 같이 보여줘요.
- **칸 정보**: 칸을 누르면 `get_true_cell`·`get_true_crop` 전체를 표로.
- **하우스**: 기온, 습도, CO2, 포자 압력 등 `get_true_house`.
- **이벤트 기록**: 4.4의 6종 전부, 틱·칸과 함께.
- **상태 해시**: 지금 틱의 `state_hash()`.
- 화면에 "개발자 패널 (진짜 값) — M7에서 측정으로 바뀌어요"라고 적어요. 기본으로 켤지, 숨길지는 질문 A1이에요.

### 5.4 M6에서 미루는 것과 임시 그림

- 미룸(M7): 증상 그림, 측정 오버레이("미지"·"오래된 정보"), 그래프, 일지, 리포트. 미룸(미정): 최종 아트 스타일(계획서 9장 미결정), 애니메이션, 사운드.
- **임시 그림 정책**: 구현자가 직접 만든 단순 도형·단색 타일만 써요(외부 에셋·폰트 내려받기 없음 → 라이선스 문제 없음). 파일 이름에 `placeholder_`를 붙이고 `game/art/README.md`에 "전부 임시"라고 적어요. 임시 그림은 **구분만 되면** 충분해요(예쁠 필요 없음).

## 6. 입력

### 6.1 조작

- 영역 고르기: 왼쪽 끌기로 직사각형(→ `TileArea`), 한 번 누르면 1칸, "전체 선택" 단추. 코어 명령의 영역이 직사각형 하나라서 떨어진 칸 여러 개는 명령을 여러 번 넣어요.
- 행동 패널: 영역 행동은 영역을 고른 뒤 행동 → 종류(id) → 양 → **실행**. 하우스 행동은 하우스 패널에서 바로.
- 단축키: 스페이스(일시정지), 숫자 1~4(배속), F3(개발자 패널), F5/F9(빠른 저장·불러오기). 키 재설정 없음.
- 거부된 명령: `command_rejected`를 화면 아래 알림으로 보여줘요(M6는 코어의 영어 메시지 그대로 + 한국어 한 줄 요약, 구현자 판단 B8).

### 6.2 행동 목록 (전부 4.3의 명령 하나로 1:1)

| 플레이어 행동 | 명령 | 입력값 | 선택지 원천 |
| --- | --- | --- | --- |
| 물 주기 | `irrigate` | 영역, mm | - |
| 퇴비 넣기 | `add_compost` | 영역, 종류, kg/m² | `params.composts` |
| 비료 주기 | `add_fertilizer` | 영역, 종류, g/m² | `params.fertilizers` |
| 석회 넣기 | `add_lime` | 영역, 종류, g/m² | `params.limes` |
| 경운 | `till` | 영역 | - |
| 멀칭 덮기·걷기 | `set_mulch` | 영역, 종류 | `params.mulches` + `none` |
| 약제 소독 | `apply_disinfectant` | 영역, 종류, g/m² | `params.disinfectants` |
| 유익균 넣기 | `add_inoculant` | 영역, 종류, g/m² | `params.inoculants` |
| 정식 | `plant` | 모종 등급, 포기/m² | `params.seedling_grades` |
| 적화·적과 | `thin_flowers` | 영역, 포기당 개수 | - |
| 병든 포기 뽑기 | `remove_plant` | 영역 | - |
| 천적 방사 | `release_predators` | 영역, 종류, 마리/m² | `params.predators` |
| 측창 여닫기 | `set_window` | 겹, 좌/우, 개방 단계 | `house.window_open_levels` |
| 난방기 | `set_heater` | 켜기/끄기, 설정 온도 | `heater_setpoint_min/max_degC` |
| 유동팬 | `set_fan` | 켜기/끄기 | - |
| 피복 겹수 바꾸기 | `set_cover_layers` | 겹수 | `min/max_cover_layers`, 기간 `cover_change_window_doy` |
| 점적 호스 깔기 | `install_drip` | - | - |

- 양 입력칸의 기본값·빠른 선택 값(예: 물 4 mm)은 게임 쪽 설정 파일의 "게임용 임의값"이에요. 시뮬레이션 파라미터가 아니에요.
- 종류의 한국어 이름표는 게임 쪽 표예요(코어에는 id만 있어요). 표에 없는 id는 id 그대로 보여줘요(데이터에 종류를 더해도 코드 수정 없이 동작).
- 태양열 소독은 명령 하나가 아니라 **물 주기 → 투명 필름 멀칭 → 창 닫기**의 조합이에요(disinfection 명세). M6는 묶음 단추를 만들지 않아요.

### 6.3 없는 행동

수동 수확(자동), 적엽·살균제(코어에 없음), 측정·검정·장비 구매(M7), 구매·판매(M7), 일반 예약·반복 명령(비료·창 같은 다른 행동을 걸어 두는 기능: 코어 일정표는 시나리오 파일용이에요). **물 주기만 예외로 자동 물 주기가 있어요(6.5, 결정 D5).**

### 6.5 자동 물 주기 (결정 D5, 2026-11-20)

263일 동안 매일 물을 손으로 주면 "화면에서 1작기 플레이" 확인(H1)이 어려워서, M6에는 **정해 둔 시각에 매일 물을 주는 자동 물 주기를 기본으로** 넣어요.

- 동작: 켜져 있으면 매일 정한 시각의 틱 직전에 게임 쪽(`SimRunner`)이 밭 전체(`whole_grid`)에 `irrigate` 명령을 하나 넣어요. 플레이어가 넣은 명령과 똑같이 명령 기록에 남아서, 기록을 CLI 일정표로 내보내 다시 돌리면 같은 결과가 나와요. **코어 변경은 없어요**(코어 일정표·새 명령·새 상태를 쓰지 않아요).
- 설정(하우스 패널): 켜기/끄기, 시각(0~23시), 양(mm). 기본값은 **켜짐, 8시, 4 mm**예요(`season_standard`의 가을·봄 처방과 같은 값, 겨울 처방은 2.5 mm). 기본값은 게임 쪽 설정 파일의 "게임용 임의값"이에요.
- 정식 전(빈 밭)에도 켜져 있으면 물을 줘요. 필요 없으면 플레이어가 꺼요. 손으로 주는 물 주기(6.2)는 그대로 쓸 수 있고 둘은 더해져요.
- 세이브: 켜짐·시각·양은 세이브 묶음의 게임 쪽 구역에 저장해요(코어 세이브 형식과 무관).
- "자동 처방 관전" 판에서는 기본이 꺼짐이에요(시나리오 일정표에 물 주기가 이미 있어요).
- **M7에서 바뀌어요**: 경제가 들어오면 자동 물 주기는 돈을 주고 사는 장비(자동 관수 장치·양액기)의 기능이 되고, 기본은 **수동 밸브로 물을 틀고 잠그는 것**이에요. 살지 말지가 플레이어 판단 요소가 돼요. M6의 자동 물 주기는 그 전까지의 편의 기능이에요.
- 확인: E10(10.1), H9(10.2).

### 6.4 진짜 값과 M6 플레이

M6에는 측정이 없어서, 진짜 값을 완전히 가리면 플레이어는 포기 크기와 꽃·과실만 보고 농사를 지어야 해요. 반대로 진짜 값을 게임 화면에 섞으면 M7의 "정보에는 비용이 들어요"를 미리 깨요. 그래서 **게임 화면(가린 것)과 개발자 패널(진짜 값)을 코드와 화면에서 갈라 두고**, M6 완료 판정은 개발자 패널을 켠 채로 해요(권장, A1).

## 7. 세이브·불러오기

### 7.1 원칙

- 내용은 코어가 만들어요: `World::serialize()`의 바이트를 **그대로** 써요(파라미터·시나리오·기상 표·동적 상태·난수 상태 포함, 형식 v8). 엔진은 그 바이트를 해석하지 않아요.
- 세이브에 파라미터가 들어 있으므로, 저장 뒤에 `data/params.toml`이 바뀌어도 그 세이브는 **저장 당시 값으로** 이어져요(결정론 유지).
- 불러온 뒤 이어서 돌린 결과는 저장 없이 계속 돌린 결과와 비트 단위로 같아야 해요(ADR-0004 규칙 6, 코어 테스트로 이미 검증. 엔진 경유는 10장 E3).

### 7.2 파일

- 위치: `user://saves/`. Windows에서는 `%APPDATA%\Godot\app_userdata\<프로젝트 이름>\saves\`예요(Godot 문서 기준).
- 한 세이브 = 파일 하나(확장자 구현자 판단 B6). 엔진 쪽 묶음 형식:

| 구역 | 내용 |
| --- | --- |
| 머리글 | 묶음 매직, 묶음 형식 버전(M6 = 1), 각 구역 길이 |
| 요약(JSON) | 세이브 이름, 실제 저장 시각, 게임 틱·날짜, 시나리오 이름, 작기 길이, 일정표 사용 여부, 게임 버전, 코어 세이브 버전(C1) |
| 엔진 상태(JSON) | 남은 일정표 위치(시나리오 일정표는 코어 세이브에 없어요: `Scenario` 주석 "명령 일정표는 코어가 아니라 로더·CLI·테스트가 다뤄요"), 일정표 전체, 지금까지의 명령 기록, 배속 |
| 코어 바이트 | `World::serialize()` 결과 그대로 |

- 요약의 "실제 저장 시각"은 표시용이고 시뮬레이션에 들어가지 않아요.
- 쓰기는 임시 파일에 쓴 뒤 이름을 바꿔요(쓰다 꺼져도 이전 세이브가 남게).
- 칸 구성(권장, 질문 A4): 수동 저장 여러 칸 + 빠른 저장 1칸 + 자동 저장 1칸(게임 날짜로 7일마다, 그리고 작기 종료 때).

### 7.3 버전

- 버전이 둘이에요: **묶음 버전**(엔진 쪽)과 **코어 세이브 버전**(v8, 코어가 필드를 더할 때마다 올라요. M0~M5b 동안 8번 바뀌었어요).
- 코어는 버전이 다르면 `UnsupportedVersion`을 돌려주고 불러오지 않아요(변환 없음).
- M6 처리(권장, 질문 A6): **불러오기를 거부하고 안내해요.** 목록에서 그 세이브를 흐리게 하고 "이 세이브는 다른 버전의 게임에서 만들었어요 (세이브 v7, 지금 v8)"라고 보여줘요. 파일은 지우지도 덮어쓰지도 않아요.
- 묶음에 시나리오 이름과 명령 기록이 들어 있어서, 나중에 "같은 시나리오를 처음부터 같은 명령으로 다시 돌려 복구"(비트 동일은 아니지만 같은 날짜·같은 행동)를 붙일 수 있어요. M6에는 만들지 않아요.
- 깨진 파일(`InvalidData`, `Truncated`)도 같은 방식으로 거부·안내해요. 게임이 죽지 않아야 해요(10장 E5).

### 7.4 명령 기록과 재생

- `SimRunner`는 받아들여진 명령을 전부 `{tick, seq, source: "player"|"schedule", command}`로 기록해요(거부된 명령은 상태에 영향이 없어 기록하지 않아요).
- 재생: 같은 시나리오를 새로 만들고 일정표를 끈 뒤, 기록의 명령을 그 틱 직전에 `seq` 순서로 넣으며 끝 틱까지 돌려요 → 최종 해시가 원래 판과 같아야 해요.
- 내보내기: `format_schedule_toml(기록)`로 `[[schedule]]` 글자를 만들고, 원래 시나리오의 일정표 자리에 넣은 파일을 `user://replays/`에 써요. 이 파일은 `sim-cli`가 그대로 읽어요.

## 8. 빌드와 꾸러미

버전·옵션의 결정과 근거는 [ADR-0015](../decisions/0015-godot-version-and-gdextension-build.md)에 있어요. 여기는 요약과 저장소 배치예요.

### 8.1 요약

| 항목 | 내용 |
| --- | --- |
| 엔진 | Godot **4.7.2** 안정판, 표준 빌드(.NET 아님) |
| 바인딩 | godot-cpp **10.0.0-stable**, `GODOTCPP_API_VERSION=4.7`, `FetchContent` 태그 고정 (**확인 필요**: 태그 내용, P0) |
| 타깃 | `sim-godot` (공유 라이브러리) → `sim-core`, `sim-data`, `godot-cpp` |
| 스위치 | `SIM_BUILD_GODOT`(기본 OFF). 기존 `vs2022`·`ci` 프리셋은 그대로 |
| 프리셋 | `godot`(구성, `build/godot`), `godot-debug`·`godot-release`(빌드), `godot`(테스트) |
| 런타임·예외 | `CMAKE_MSVC_RUNTIME_LIBRARY`를 지금과 같은 값으로 명시, `GODOTCPP_DISABLE_EXCEPTIONS=OFF` (godot-cpp 기본값이 /MT·`_HAS_EXCEPTIONS=0`이라서) |
| Godot 위치 | CMake 캐시 `GODOT_EXECUTABLE` 또는 환경 변수 `GODOT_BIN`. 저장소에 경로를 적지 않아요. 없으면 엔진 테스트만 건너뛰어요 |
| 산출물 | `sim-godot` DLL을 빌드 뒤 `game/bin/`에 복사(커밋 안 함). `data/`도 `game/data/`로 복사(커밋 안 함) |
| 추가 도구 | Python 3(godot-cpp 바인딩 생성. 이미 `.venv`용으로 3.14가 있어요), git |

- 편집기와 개발 실행은 디버그 기능 표시(`windows.debug.x86_64`)의 라이브러리를 읽어요(Godot 문서의 `.gdextension` 예시 기준). `GODOTCPP_TARGET`(`template_debug`/`template_release`/`editor`)이 구성 시점 값이라 Visual Studio 다중 구성과 어떻게 맞출지는 **확인 필요**예요(P0).
- 새로 체크아웃한 뒤 헤드리스로 바로 실행되는지, 먼저 `--import`로 프로젝트를 한 번 훑어야 확장이 등록되는지 **확인 필요**예요(P1).

### 8.2 저장소 배치 (추가분)

| 경로 | 내용 | 커밋 |
| --- | --- | --- |
| `sim-godot/` | 래퍼 C++(`SimWorld`, 등록 코드), `CMakeLists.txt` | O |
| `game/project.godot` | Godot 프로젝트 | O |
| `game/under_the_bed.gdextension` | 확장 선언(진입 기호, `compatibility_minimum = "4.7"`, 라이브러리 경로) | O |
| `game/scenes/`, `game/scripts/` | 장면(`.tscn`)과 GDScript | O |
| `game/config/` | 게임 쪽 설정: 배속 단계, 표현 문턱, 양 기본값, 한국어 이름표, 시나리오 목록(경로·길이) | O |
| `game/art/` | 임시 그림(`placeholder_*`) | O |
| `game/tests/` | 헤드리스 검사 스크립트 | O |
| `game/bin/`, `game/data/`, `game/.godot/` | 빌드 산출물·복사본·엔진 캐시 | X (.gitignore 추가) |
| `tools/compare_engine_cli.py` | 엔진 대 CLI 틱별 해시 비교 | O |

- Godot 프로젝트를 저장소 최상위가 아니라 `game/`에 두는 이유: 편집기가 `build/`, `runs/`, `.venv/`를 자원으로 훑지 않게 하려고요.
- `CLAUDE.md`의 "디렉터리"·"빌드와 실행" 표는 구현 PR에서 갱신해요(이 초안은 건드리지 않아요).

### 8.3 CI

- 지금 워크플로(`.github/workflows/ci.yml`)는 그대로 둬요. 엔진용 작업을 따로 추가해요: Godot 4.7.2를 내려받아(체크섬 확인) `GODOT_EXECUTABLE`로 넘기고, `godot` 프리셋으로 빌드·`ctest`를 돌려요.
- 단, CLAUDE.md에 적힌 대로 **원격 저장소가 없어 CI 실행은 지금도 검증하지 못해요.** M6에서도 워크플로 파일만 쓰고 실행 검증은 건너뛰어요. 실제 판정은 이 PC에서 `ctest --preset godot`으로 해요.
- 내보내기 템플릿 내려받기와 내보내기 검사는 A8에 따라요(권장: M6에는 없음).

## 9. 틱 처리와 시뮬레이션에 미치는 영향

**없어요.** 틱 처리 순서, 상태 변수, 파라미터, 세이브 형식(v8), 스키마 버전(7), 1작기 기준값(`tests/baselines/m4_season_summary.json`)이 모두 그대로예요. M6 내내 기존 `ctest`(565개)와 `tools/season_regression.py`가 **한 글자도 바뀌지 않고** 통과해야 해요(10장 E0). 코어 변경 C1~C3는 공개 선언만 더해요.

## 10. 수용 기준과 테스트

계획서 완료 기준을 둘로 풀어요: **(가) CLI 결과와 동일** = E1~E4가 자동으로 통과, **(나) 화면에서 1작기 플레이 가능** = 10.2 목록을 디렉터가 직접 확인.

### 10.1 자동 검사

엔진 검사는 `ctest --preset godot`에 등록해요(`GODOT_EXECUTABLE`이 있을 때). 형식: `godot --headless --path game --script res://tests/<검사>.gd -- <인자>` → 종료 코드. 테스트 이름은 ASCII 영어예요.

| ID | 무엇을 | 방법 | 기대 |
| --- | --- | --- | --- |
| E0 | 코어가 그대로인가 | `godot` 프리셋 트리에서 기존 `sim-tests` 전체 + `tools/season_regression.py`(그 트리의 `sim-cli`로) | 전부 통과, 기준값 대비 변화 없음. `vs2022`·`ci` 트리의 최종 해시와 `godot` 트리의 최종 해시가 같음 |
| E1 | 확장이 로드되는가 | 헤드리스로 `SimWorld`를 만들고 `empty.toml`을 읽어 틱 0 해시 출력. 엔진 버전이 4.7.2인지도 확인 | 종료 코드 0, CLI의 틱 0 해시와 같음 |
| **E2** | **CLI와 같은 결과 (완료 기준)** | `tools/compare_engine_cli.py`: 같은 트리·같은 구성의 `sim-cli --out`(틱마다 `state_hash` 열)과 헤드리스 엔진 실행(`틱,해시` 출력)을 **모든 틱**에서 비교. 대상: `empty`(8,760틱), M4 `season_*` 7개(6,312틱), M5a·M5b에서 소독·정식·팬·천적·포기 제거 명령이 든 시나리오 각 1개 이상(`--remove-on-onset`을 쓰는 `s28_overhead_remove`는 CLI 전용 동작이라 제외) | 모든 틱의 해시가 같음. 다르면 처음 달라진 틱을 보고 |
| E3 | 엔진 경유 세이브 왕복 | N틱 → 묶음 파일로 저장 → 새 `SimWorld`로 불러오기 → M틱. N+M틱 연속 실행과 비교. 일정표가 있는 시나리오로(남은 일정표 복원 확인) | 최종 해시 같음, 다시 저장한 코어 바이트도 같음 |
| E4 | 명령 기록 재생 (엔진 → 엔진, 엔진 → CLI) | 일정표를 끄고 고정된 "가짜 플레이어" 스크립트가 17종 명령을 여러 틱에 걸쳐 넣음(거부되는 것 포함) → 기록으로 엔진에서 재생, `[[schedule]]`로 내보내 `sim-cli`로 실행 | 세 실행의 최종 해시가 같음 |
| E5 | 잘못된 세이브 | 버전을 바꾼 파일, 중간에 자른 파일, 빈 파일, 매직이 다른 파일 | 각각 `UnsupportedVersion`/`Truncated`/`InvalidData`를 받고 프로세스가 살아 있음. 원본 파일은 그대로 |
| E6 | 명령 변환 | 17종 각각을 사전 → `enqueue` → 기록 → 사전으로 되돌려 비교. 모르는 키·빠진 키·타입 불일치·모르는 `action` | 왕복 일치, 잘못된 사전은 `InvalidArgument`이고 큐가 늘지 않음 |
| E7 | 배속과 무관 | 같은 명령 기록을 (1틱씩 / 프레임당 7틱 / 한 번에 전부) 세 가지 `advance` 묶음으로 실행 | 최종 해시 같음 |
| E8 | 진짜 값 누출 없음 | `game/scripts/`에서 개발자 패널 폴더 밖의 파일에 `get_true_` 호출이 없는지 글자 검색(Python 도구, ctest 등록) | 0건 |
| E9 | 스냅샷 변환 | 헤드리스에서 `get_true_column`·`get_true_cell`의 몇 필드 평균을 같은 틱의 CLI CSV 값과 비교(예: `water_mm`, `ph`, `leaf_g_per_m2`) | CSV에 찍힌 자릿수까지 같음 |

- E2가 잡으려는 것: 래퍼의 일정표 끼우기 실수(한 틱 어긋남), 데이터 읽기 경로 차이(기상 CSV 경로·줄바꿈), 빌드 옵션이 갈라진 것(ADR-0015), Godot 프로세스의 부동소수점 상태(R2).
- **E10 (자동 물 주기, 결정 D5)**: 헤드리스로 자동 물 주기(8시, 4 mm)만 켜고 30일을 돌린 뒤 명령 기록을 일정표로 내보내 `sim-cli`로 다시 돌리면 모든 틱의 `state_hash`가 같아요. 기록에는 하루에 `irrigate` 하나씩만 있어요.
- 래퍼 C++만의 단위 테스트 틀(Catch2에 godot-cpp 붙이기)은 만들지 않아요. 래퍼는 헤드리스 스크립트로 검사해요(구현자 판단 B9).
- 시나리오 그래프(수용 기준 "그래프가 기대 방향")는 M6에 새 프로세스가 없어서 해당하지 않아요. 대신 PR에 **E2 비교 결과표**(시나리오별 틱 수·일치 여부)와 **화면 사진**을 붙여요.

### 10.2 화면에서 손으로 확인 (디렉터, 개발자 패널을 켠 채로)

| # | 확인 | 통과 |
| --- | --- | --- |
| H1 | "직접 플레이" 판을 시작해 정식 → 물·비료·창·난방 관리 → 수확까지 263일을 끝낼 수 있다 | 종료 화면이 뜨고 수확 합계가 0보다 크다 |
| H2 | 6.2의 행동 17가지를 각각 화면에서 넣을 수 있고, 다음 시간에 적용된 것이 개발자 패널 값으로 보인다 | 17개 전부 |
| H3 | 거부되는 행동(작물이 있을 때 정식, 휴작기 밖 겹수 변경, 그리드 밖 영역)을 넣으면 알림이 뜨고 게임이 계속된다 | 알림이 이유를 말함 |
| H4 | 배속 4단계와 일시정지가 동작하고, 가장 빠른 배속에서도 화면이 멈추지 않는다 | 끊김 없이 조작 가능 |
| H5 | 저장 → 게임 종료 → 다시 켜서 불러오기 → 같은 날짜·같은 화면 | 해시가 저장 때와 같음 |
| H6 | "자동 처방 관전"으로 `season_standard`를 끝까지 돌리면 종료 화면의 최종 해시가 CLI 출력(`final_state_hash`)과 같다 | 같음 |
| H7 | 포기가 자라는 것(크기 단계), 꽃·과실, 죽은 포기, 뽑은 칸, 멀칭이 눈으로 구분된다 | 구분됨 |
| H8 | 개발자 패널을 끄면 게임 화면에 진짜 값 숫자가 하나도 없다 | 없음 |
| H9 | 자동 물 주기를 켜면 정한 시각마다 물이 들어가고, 끄거나 시각·양을 바꾸면 다음 날부터 그대로 따른다. 일시정지한 채 영역을 끌어 고르고 행동을 넣을 수 있다 | 개발자 패널의 수분 값과 대기 명령 수로 확인 |

## 11. 리뷰 체크리스트 대응

| # | 항목 | M6에서 |
| --- | --- | --- |
| 1 | 코어가 엔진·OS와 무관 | `sim-core`는 godot-cpp를 모르고, 변경은 C1~C3 선언뿐 |
| 2 | 결정론 | E2·E3·E4·E7 |
| 3 | 상수는 데이터 파일에서 | 시뮬레이션 상수 추가 없음. 표현·조작 값은 `game/config/` |
| 4 | 단위 명시 | 사전 키 = 코어 필드 이름 그대로 |
| 5 | 보존 법칙 | E0(기존 테스트 그대로) |
| 6 | 시나리오 그래프 | 해당 없음 → E2 결과표 + 화면 사진 |
| 7 | 명세 갱신 | 이 문서, core-api(C1~C3), ADR-0015, CLAUDE.md 빌드 항목 |

## 12. 위험

| # | 위험 | 대응 |
| --- | --- | --- |
| R1 | godot-cpp CMake 기본값이 우리 빌드 옵션을 바꿈(/MT, `_HAS_EXCEPTIONS=0`) → 기존 결과가 달라지거나 링크 오류 | ADR-0015 결정 5, E0. P0에서 가장 먼저 확인 |
| R2 | Godot 프로세스 안에서 부동소수점 제어 상태(반올림·비정규수 처리)가 CLI와 다름 → 해시 불일치 | E2가 틱 단위로 잡아요. 나오면 래퍼가 `step` 앞뒤로 상태를 맞춰요(코어 수정 아님). **확인 필요** (P1) |
| R3 | godot-cpp의 CMake가 보조 지원이라 Visual Studio 다중 구성·C++23 조합에서 막힘 | P0. 막히면 ADR-0015 대안 1(godot-cpp만 SCons로 미리 빌드) → 새 ADR |
| R4 | M6 화면이 진짜 값을 보여줘 M7 정보 계층 설계를 굳혀 버림 | `get_true_*` 이름 분리, 개발자 패널 폴더 분리, E8, H8 |
| R5 | 범위 폭발(일지·그래프·증상·경제가 "화면이 생겼으니" 끼어듦) | 1.2 표. 이슈는 13장 목록만 |
| R6 | 가장 빠른 배속에서 프레임 끊김(디버그 빌드는 틱이 훨씬 느려요) | 프레임당 틱 상한. 스냅샷 복사는 프레임당 한 번. 디버그 빌드 틱 시간은 **확인 필요**(P2) |
| R7 | 세이브 형식이 마일스톤마다 바뀌어 M6 세이브가 M7에서 못 쓰게 됨 | A6 권장안(거부 + 안내). 개발 중에는 감수 |
| R8 | 엔진 버전을 올리면 바인딩이 깨짐(ADR-0001 감수 사항) | 패치 버전까지 고정, 올릴 때 E1·E2 재실행 |
| R9 | 뷰어 때문에 코어 구조를 바꾸고 싶어짐(계획서 7장 "없어야 해요") | 코어 변경은 C1~C3만. 더 필요하면 멈추고 보고 |
| R10 | 편집기가 `.tscn`·`project.godot`을 다시 저장하며 무관한 줄을 바꿔 PR이 지저분해짐 | 장면은 작게 나누고, PR마다 장면 변경을 확인 |
| R11 | CI를 실행 검증하지 못함 | 로컬 `ctest --preset godot`이 판정 기준 |

## 13. 작업 나누기 (이슈 1개 = 기능 1개 = PR 1개)

| # | 이슈 | 수용 기준 | 앞선 이슈 |
| --- | --- | --- | --- |
| 1 | 빌드 통합: `SIM_BUILD_GODOT`, godot-cpp 가져오기, `godot` 프리셋, `game/` 뼈대, 빈 확장 | E0, E1(로드만) | ADR-0015 승인 |
| 2 | 코어 공개 API C1~C3 + core-api 명세 갱신 | 기존 테스트 그대로 + 새 단위 테스트 | 이 명세 승인 |
| 3 | `SimWorld`: 데이터 읽기, `advance`, `state_hash`, 일정표 | E1, **E2** | 1 |
| 4 | `SimWorld`: 명령 사전 ↔ `sim::Command`, 이벤트, 명령 기록 | E6, E4(엔진 → 엔진) | 3 |
| 5 | `SimWorld`: 스냅샷 접근자(보이는 것, 진짜 값) | E9 | 3 |
| 6 | 엔진 대 CLI 비교 도구와 ctest 등록, 기록 → `[[schedule]]` 내보내기 | E2 전체 목록, E4(엔진 → CLI) | 3, 4 |
| 7 | `SimRunner`: 배속·일시정지·프레임 상한, HUD(날짜·배속·대기 명령) | E7, H4 | 3 |
| 8 | 타일 렌더링: 흙·멀칭·포기·꽃·과실, 카메라, 임시 그림 | H7 | 5, 7 |
| 9 | 입력 1: 영역 선택 + 영역 행동 12종 | H2(영역), H3 | 4, 8 |
| 10 | 입력 2: 하우스 패널(창·난방·팬·겹수·점적·정식) | H2(하우스), H3 | 4, 7 |
| 11 | 세이브·불러오기: 묶음 형식, 칸, 버전 안내 | E3, E5, H5 | 2, 4, 7 |
| 12 | 개발자 패널: 오버레이, 칸 정보, 하우스, 이벤트 기록, 해시 | E8, H8 | 5, 8 |
| 13 | 시나리오 고르기 + 작기 종료 화면(합계·해시) + "직접 플레이" 시나리오 파일 | H1, H6 | 7, 9, 10 |
| 14 | CI 엔진 작업(워크플로 파일) | 파일 검토(실행 검증 불가) | 6 |
| 15 | (A8에 따라) 내보내기 | 내보낸 실행 파일로 H6 | 13 |

- 13번의 "직접 플레이" 시나리오는 `data/scenarios/m6/`의 새 파일이에요(A3). 기존 시나리오·기준값은 건드리지 않아요.

## 14. 프로토타입 계획 (초안 승인 뒤, 버리는 브랜치 `proto/m6-check`)

M5와 같은 방식이에요: 초안 승인 → 프로토타입으로 막히는 곳을 먼저 찾기 → 명세 수정 사항을 모아 한 번에 최종 승인 → `feature/m6`에서 13장 순서로 본구현.

| 차수 | 목적 | 확인할 것 | 통과 기준 |
| --- | --- | --- | --- |
| P0 | 빌드 사슬 | godot-cpp 10.0.0-stable을 FetchContent로 받아 `sim-godot` 빈 확장을 빌드. ADR-0015의 "확인 필요" 전부(태그 내용, 런타임 설정, 예외 옵션, C++17/23 혼합, `GODOTCPP_TARGET`과 다중 구성). 처음 빌드 시간 | Godot 4.7.2에서 확장이 로드됨. `godot` 트리의 기존 테스트·1작기 회귀가 그대로(E0) |
| P1 | CLI와 같은 결과 | `load_files` + `advance` + `state_hash`만 만든 래퍼로 `season_standard` 6,312틱 비교. 헤드리스 첫 실행 절차(`--import` 필요 여부). 부동소수점 상태(R2) | 모든 틱 해시 일치(E2의 한 시나리오) |
| P2 | 화면과 속도 | 임시 타일로 80칸 그리기, 배속 4단계, 디버그·릴리스 빌드의 프레임 시간 | "열흘 = 1초"에서 끊김 없음(릴리스). 디버그 빌드 한계 기록 |
| P3 | 입력과 세이브 | 명령 3~4종(물, 비료, 창, 정식) 넣기, 저장·불러오기, 기록 재생 | E3·E4의 축소판 통과 |

프로토타입 뒤 최종 승인 요청 목록에는: ADR-0015의 확인 결과, 바뀐 바인딩 표면, 측정한 성능, 프로토타입에서 나온 새 질문을 넣어요.

## 15. 디렉터 결정이 필요한 질문

### (0) 디렉터 결정 (2026-11-20)

| # | 질문 | 결정 | 반영한 곳 |
| --- | --- | --- | --- |
| D1 | A1 숫자 표시 | 권장대로: 게임 화면은 눈에 보이는 것만, 숫자는 개발자 패널(M6 기본 켜짐) | 5장, 6.4 |
| D2 | A2 시간 속도 | 권장대로: 일시정지 / 1시간 = 1초 / 6시간 = 1초 / 하루 = 1초 / 열흘 = 1초. 디렉터는 "1시간 = 현실 3~4분"을 검토했지만(칸마다 손볼 시간), 한 작기가 약 368시간이 되어 **일시정지 중 명령 + 영역 끌어 고르기(6.1)** 로 행동 시간을 확보하기로 했어요. **"행동에 게임 시간이 든다"는 설계는 노동력·경제와 함께 M7에서** 다뤄요 | 3.4, 6.1, H9 |
| D3 | A3 플레이 판 | 권장대로: 직접 플레이(빈 밭에서 밑거름·정식부터) + 자동 처방 관전 | 3.4, 10.2 |
| D4 | A4 저장 | 권장대로: 자유 저장(수동 여러 칸 + 빠른 저장 + 7일마다 자동 저장). 되돌리기 제한은 M7에서 판단 | 7장 |
| D5 | A5 반복 예약 | **자동 물 주기를 M6 기본 기능으로**(정한 시각에 매일, 기본 켜짐). 일반 예약 기능은 없음. **M7 경제 이후에는 구매 장비(자동 관수 장치·양액기)의 기능으로 빼고 기본은 수동 밸브** | 6.3, 6.5, E10, H9 |
| D6 | A6 옛 세이브 | 권장대로: 버전이 다르면 불러오기를 막고 안내, 파일은 유지 | 7장 |
| D7 | A7 칸 그림 | 권장대로: 칸마다 대표 포기 그림 하나 | 5장 |
| D8 | A8 내보내기 | 권장대로: M6에서는 만들지 않음(완료 판정은 편집기 실행) | 8장 |
| D9 | A9 버전·코어 추가 | **ADR-0015 승인**(Godot 4.7.2 + godot-cpp 10.0.0-stable), 코어 공개 API 추가 C1~C3 승인 | 2.3, 8장, ADR-0015 |

아래 (a)는 질문 원문 기록이에요. (b)는 따로 말이 없어 초안대로 구현자가 정해요.

### (a) 디렉터 판단이 필요한 것

1. **A1. M6 화면에 숫자를 어디까지 보여줄까요?** 측정 기능은 M7이라 M6에는 "진짜 값"밖에 없어요.
   - 권장: 게임 화면에는 눈에 보이는 것(포기 크기, 꽃·과실, 죽은 포기, 멀칭, 내가 맞춘 창·난방 상태, 바깥 날씨)만 두고, 흙·작물·하우스의 숫자는 전부 **"개발자 패널(진짜 값)"** 에 모아요. M6에서는 이 패널을 기본으로 켜 두고, M7에서 측정이 생기면 숨겨요.
   - 얻는 것: M7의 "정보에는 비용이 들어요"를 미리 깨지 않아요. 잃는 것: M6의 화면은 게임이라기보다 검증 도구처럼 보여요.
   - 다른 길: 수분·기온 같은 몇 가지를 미리 게임 화면에 넣기(M7에서 다시 떼어내야 할 수 있어요).
2. **A2. 시간이 얼마나 빨리 흐를까요?**
   - 권장: 일시정지 / 1시간 = 1초 / 6시간 = 1초 / 하루 = 1초 / 열흘 = 1초. 한 작기(263일)가 "하루 = 1초"로 약 4분 반이에요.
   - 따져 볼 것: 가장 느린 단계가 너무 느린지(하루가 24초), 가장 빠른 단계에서 창을 제때 못 닫는 게 괜찮은지. 숫자는 나중에 설정 파일에서 쉽게 바꿀 수 있어요.
3. **A3. "화면에서 1작기 플레이"는 어떤 판으로 할까요?**
   - 권장: 두 가지를 넣어요. ① **직접 플레이**: `season_standard`와 같은 밭·날씨·모종에서 일정표 없이 플레이어가 전부 해요(9월 10일부터 263일). ② **자동 처방 관전**: 기존 시나리오를 일정표대로 돌려 보기만 해요(CLI와 같은 결과 확인용). 관전 중에도 명령을 넣을 수 있어요.
   - 따져 볼 것: ①의 시작을 "이미 심긴 밭"으로 할지 "빈 밭에서 밑거름·정식부터"로 할지. 권장은 **빈 밭에서 시작**(퇴비·정식 행동을 쓸 수 있게)이에요.
4. **A4. 저장을 마음대로 하게 둘까요?**
   - 권장: M6는 **자유 저장**(수동 여러 칸 + 빠른 저장 + 7일마다 자동 저장 1칸)이에요. 검증과 디버깅에 필요해요.
   - 따져 볼 것: 하드코어 게임이라 "되돌리기"를 막고 싶을 수 있어요(처방이 틀렸으면 불러오기로 무르기). 막을지는 M7에서 플레이해 보고 정하는 것을 권장해요.
5. **A5. 반복 예약 행동("매일 8시 물 4 mm")을 M6에 넣을까요?**
   - 권장: **넣지 않아요.** M6는 플레이어가 그때그때 넣어요. 263일 동안 매일 물을 주는 것은 번거롭지만, 예약 기능은 새 화면 요소이고 자동 개폐기·경제와 함께 M7에서 설계하는 편이 맞아요.
   - 잃는 것: M6의 직접 플레이는 손이 많이 가요(완료 확인 H1이 지루할 수 있어요). 다른 길: "어제와 같은 물 주기를 매일 반복" 단추 하나만 넣기.
6. **A6. 게임이 바뀐 뒤 옛 세이브는 어떻게 할까요?** 코어 세이브 형식은 마일스톤마다 바뀌어 왔어요(지금 v8).
   - 권장: **불러오기를 막고 안내만 해요**("다른 버전에서 만든 세이브예요"). 파일은 지우지 않아요. 옛 세이브를 새 형식으로 바꿔 주는 기능은 출시 전에 다시 정해요.
   - 잃는 것: M7 작업 중에 M6 세이브는 못 써요. 얻는 것: 변환 코드를 마일스톤마다 쓰지 않아요.
7. **A7. 한 칸(1 m², 포기 8개)에 무엇을 그릴까요?**
   - 권장: **칸마다 대표 포기 그림 하나**예요. 상태가 칸 단위라서 한 칸의 포기 8개는 어차피 똑같아요.
   - 다른 길: 재식 밀도만큼 작은 포기를 여러 개 그리기(보기에는 밭 같지만 "포기마다 다르다"는 오해를 줘요). 계획서 9장 "타일 해상도" 미결정과 닿아 있지만, 코어는 이미 칸 단위라 M6에서 그 결정을 바꾸지 않아요.
8. **A8. M6에서 배포용 실행 파일(내보내기)까지 만들까요?**
   - 권장: **만들지 않아요.** 완료 판정은 Godot 편집기에서 실행해서 해요. 내보내기는 M7(디렉터가 직접 플레이해 판단하는 단계)에 넣어요.
   - 이유: 내보내기 템플릿 설치, 데이터 파일 묶기, 릴리스용 확장 빌드가 따로 필요하고 M6 완료 기준("화면에서 플레이, CLI와 같은 결과")과 무관해요.
9. **A9. 엔진과 바인딩 버전 고정: Godot 4.7.2 + godot-cpp 10.0.0([ADR-0015](../decisions/0015-godot-version-and-gdextension-build.md))을 승인해 주세요.** 코어의 작은 공개 API 추가 3건(2.3의 C1~C3)도 같이요. C2·C3는 빼도 M6가 돌아가요(화면이 조금 불편해져요: 못 하는 행동을 눌러 봐야 알아요).

### (b) 따로 말씀이 없으면 구현자가 정하는 것

| # | 항목 | 정할 방향 |
| --- | --- | --- |
| B1 | 바인딩 모양 | 클래스 `SimWorld` 하나, 신호 없음(끌어오기), 실패는 `{ok, code, message}` 사전 |
| B2 | 명령 전달 | 범용 `enqueue(Dictionary)` 하나 + GDScript 쪽 도우미 함수. 키는 시나리오 `[[schedule]]`과 동일 |
| B3 | 데이터 파일 | 원본은 `data/`, 빌드가 `game/data/`로 복사. 읽기는 `FileAccess` + `sim-data` 문자열 파서 |
| B4 | 게임 쪽 설정 | `game/config/`의 Godot `ConfigFile`(.cfg). 표현 문턱·양 기본값은 "게임용 임의값" 표시 |
| B5 | 타일 크기·창 크기·색 | 임시 그림 기준으로 정함 |
| B6 | 세이브 파일 확장자, 묶음 매직·구역 순서, 임시 파일 뒤 이름 바꾸기 | 7.2대로 |
| B7 | 프레임당 틱 상한 | 프로토타입 P2 측정으로 정함 |
| B8 | 화면 글자 | 한국어 고정. 거부 알림은 코어 영어 메시지 + 한국어 한 줄 |
| B9 | 엔진 쪽 검사 방식 | 외부 테스트 애드온 없이 헤드리스 GDScript + 종료 코드, `ctest` 등록 |
| B10 | 프리셋·타깃·폴더 이름 | `godot` 프리셋, `sim-godot`, `game/` |
| B11 | godot-cpp 옵션 세부(런타임, 예외, SYSTEM 포함), 막힐 때 커밋 해시 고정 | ADR-0015 결정 5 범위 안에서 |
| B12 | 낮·밤 색조, 수확 순간 표시 같은 꾸밈 | 상태를 드러내지 않는 범위에서 |
| B13 | 단축키 | 6.1대로 |
| B14 | `.gitignore` 손보기(`game/bin/`, `game/data/`, `*.import` 정책) | Godot 공식 권장을 확인하고 따름 |

## 16. 확인하지 못한 것 (모아 보기)

| # | 내용 | 언제 확인 |
| --- | --- | --- |
| U1 | godot-cpp `10.0.0-stable` 태그가 API 4.7을 담는지, FetchContent로 문제없이 붙는지 | P0 |
| U2 | /MD 유지 + `GODOTCPP_DISABLE_EXCEPTIONS=OFF` + C++17(godot-cpp)/C++23(`sim-godot`) 조합이 MSVC에서 빌드·로드되는지 | P0 |
| U3 | `GODOTCPP_TARGET`과 Visual Studio 다중 구성(Debug/Release)을 맞추는 방법 | P0 |
| U4 | 새 체크아웃에서 헤드리스 실행 전에 `--import`가 필요한지 | P1 |
| U5 | Godot 프로세스 안의 부동소수점 제어 상태가 결과에 영향을 주는지 | P1 (E2) |
| U6 | 디버그 빌드의 틱 시간, 가장 빠른 배속의 프레임 시간 | P2 |
| U7 | `.gdextension`의 정확한 키·라이브러리 경로 표기(4.7 문서의 예시는 확인, 이 프로젝트에서 동작은 미확인) | P0 |
| U8 | Godot 4의 `*.import` 커밋 권장 여부(지금 `.gitignore`는 무시) | 이슈 1 |
| U9 | GitHub Actions에서의 엔진 작업 동작(원격 저장소 없음) | 원격이 생길 때 |
| U10 | 내보내기 때 `*.toml`·`*.csv`를 묶음에 넣는 필터 설정(문서에 "자원이 아닌 파일 필터"가 있다는 것만 확인) | A8에 따라 |
