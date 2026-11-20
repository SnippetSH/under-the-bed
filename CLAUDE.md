# CLAUDE.md — 에이전트 공통 규칙

하드코어 2D 농사 시뮬레이션(비닐하우스 딸기, 토양 진단 루프) 프로젝트예요.
전체 계획은 [docs/plan.md](docs/plan.md), 확정된 결정은 [docs/decisions/](docs/decisions/)에 있어요.
이 파일과 계획서가 충돌하면 **계획서와 승인된 ADR이 우선**이고, 충돌을 발견하면 작업을 멈추고 보고해요.

## 역할

- **SeungH (디렉터·리뷰어)**: 명세 확정, 마일스톤 승인, 밸런스 판단, 우선순위 결정
- **AI 에이전트 (구현자)**: 코드, 테스트, 명세 문서 갱신, 시나리오 그래프 생성

## 현재 마일스톤

**M6a 엔진 연결 + M6b 캐릭터 플레이** ([ADR-0016](docs/decisions/0016-m6-split-and-character-play.md), 2026-11-20 승인: M6를 둘로 나눔). **M6a**([m6-overview](docs/spec/m6-overview.md), 0장이 우선): GDExtension 바인딩, 점검 화면(격자), 세이브. 완료 기준: CLI 결과와 동일한 결과, 점검 화면에서 1작기를 끝까지 돌릴 수 있음. 최종 명세 2026-11-20 승인(m6-overview 17장), **2026-11-20 완료 승인**(`ctest --preset ci` 571개, `ctest --preset godot -R engine` 13개 통과. CI 워크플로의 엔진 검사 작업과 Debug 구성 확장 검사는 하지 않음). **M6b**([m6b-character-play](docs/spec/m6b-character-play.md), 초안 승인): 비스듬한 2.5D 화면, 캐릭터가 걸어가서 칸(포기 8개 묶음) 단위로 작업, 하루 단위 시간. 완료 기준: 캐릭터로 1작기 플레이 가능, 명령 기록이 CLI와 같은 결과. 다음은 프로토타입 P4~P6(`proto/m6b-check`). 포기 단위로 코어를 바꿀지는 그 프로토타입을 디렉터가 본 뒤 판단해요. 버전은 [ADR-0015](docs/decisions/0015-godot-version-and-gdextension-build.md)(Godot 4.7.2 + godot-cpp 10.0.0-stable). Godot 실행 파일 위치는 저장소에 적지 않고 CMake 캐시 `GODOT_EXECUTABLE` 또는 환경 변수 `GODOT_BIN`으로 받아요.
**M5(미생물·병해)는 2026-11-20 완료 승인**: M5a(토양 미생물·소독, 2026-11-19 승인, [ADR-0013](docs/decisions/0013-m5a-deep-soil-and-cell-variation.md)) + M5b(하우스 습도·지상부 병해충, 2026-11-20 승인, [ADR-0012](docs/decisions/0012-m5-split-and-population-index.md), [ADR-0014](docs/decisions/0014-m5b-transpiration-humidity-feedback.md)는 되돌림). 기록은 [m5-overview](docs/spec/m5-overview.md) 12장(결정 1~52)·15장. `ctest --preset ci` 565개 통과. 프로토타입 브랜치 `proto/m5b-check`는 기록용으로 남아 있어요.
밸런싱 때 다시 볼 것(디렉터 보류): 살균·살충제 추가(M7 이후), 천적 바깥 유입(결정 49), 겨울 백색 필름 지온(결정 23).
(M0~M3는 2026-11-18, M4·M5a는 2026-11-19, M5b는 2026-11-20 승인. 단, GitHub Actions CI는 원격 저장소가 없어 실행 검증을 건너뜀)
승인 전에 다음 마일스톤의 기능을 미리 만들지 않아요. (마일스톤이 바뀌면 이 줄을 갱신해요.)

## 작업 절차

1. **명세 먼저**: 새 프로세스·기능은 `docs/spec/`에 명세를 먼저 쓰고, 디렉터 승인 후에 코딩해요.
2. **작업 단위**: 이슈 1개 = 프로세스 1개 또는 기능 1개 = PR 1개.
3. **수용 기준**은 두 가지예요: 테스트 통과, 시나리오 그래프가 기대 방향을 보임.
4. **산출물**: 코드, 테스트, 갱신된 명세, 그래프 이미지.
5. 무엇을 왜 단순화했는지는 `docs/decisions/`에 ADR로 남겨요.
6. 새 프로세스를 추가할 때는 그 인과관계를 보여주는 시나리오 테스트를 **함께** 추가해요.

## 금지 사항

- 명세에 없는 프로세스나 변수를 임의로 추가하지 않아요.
- 테스트를 통과시키려고 파라미터를 몰래 튜닝하지 않아요. 파라미터 변경은 별도 PR로 이유와 함께 올려요.
- 디렉터 승인 없이 테스트의 기대값이나 회귀 스냅샷을 수정하지 않아요.
- 시뮬레이션 코어(`sim-core`)에 엔진, UI, 파일 입출력 코드를 넣지 않아요.
- 명세 밖 기능 추가, 작업과 무관한 파일 수정을 하지 않아요.

## 아키텍처 경계

- `sim-core`: 순수 로직. 파일·콘솔·시계·스레드·엔진 API에 의존하지 않아요. 파라미터는 이미 파싱된 구조체로 받고, 직렬화는 바이트 버퍼를 주고받는 형태로만 제공해요.
- 데이터 파일(TOML) 파싱은 코어 바깥의 로더 라이브러리가 담당해요. ([ADR-0002](docs/decisions/0002-data-format.md))
- 엔진은 코어 스냅샷을 읽어서 그리기만 하고, 플레이어 행동은 "명령"으로만 전달해요.
- 코어는 CMake로 한 번만 빌드하고, CLI·테스트·GDExtension이 같은 라이브러리를 링크해요. ([ADR-0004](docs/decisions/0004-determinism-scope.md))

## 코딩 규칙

### 결정론 ([ADR-0004](docs/decisions/0004-determinism-scope.md))

- 난수는 주입받은 시드로 코어 자체 PRNG만 써요. `std::*_distribution`, `rand()`, `std::random_device`는 금지예요.
- `/fp:fast`, `-ffast-math` 같은 부동소수점 완화 옵션은 금지예요.
- 결과가 `unordered_map`/`unordered_set`의 순회 순서에 의존하면 안 돼요.
- 코어 안에서 실제 시계, 환경 변수, 멀티스레드 비결정 동작을 쓰지 않아요.

### 단위와 상수

- 모든 물리량 변수와 파라미터는 단위를 이름에 붙여요. 예: `water_mm`, `soil_temp_degC`, `nh4_mg_per_kg`, `rate_per_hour`. 무차원 계수는 `_factor`, 0~1 지수는 `_index`를 붙여요.
- 계수, 곡선, 작물 요구량은 코드에 하드코딩하지 않고 `data/`에서 가져와요. 코드에 허용되는 리터럴은 0, 1 같은 수학적 상수뿐이에요.
- 출처가 없는 수치는 데이터 파일에 "게임용 임의값"으로 표시해요.

### 수치 안전

- 프로세스는 "이번 틱의 변화량"을 계산하고, 실제 이동량은 원천 풀의 남은 양으로 제한해요. 어떤 풀도 음수가 되면 안 돼요.
- 극한 입력(비료 100배, 관수 0, 극단 기온)에서도 NaN이나 발산이 없어야 해요.

### 언어

- 코드 식별자는 영어, 문서와 커밋 설명은 한국어로 써요. 주석은 둘 다 괜찮아요.
- **테스트 케이스 이름(`TEST_CASE("...")`)은 ASCII 영어로 써요.** Windows에서 CTest가 비ASCII 이름을 Catch2에 넘길 때 인코딩이 깨져 테스트가 실패해요. 한국어 설명은 바로 위 주석에 달아요. 이름에 `[`도 쓰지 않아요(태그로 해석됨).

## 리뷰 체크리스트 (디렉터용, PR 설명에도 이 항목별로 결과를 적어요)

1. 코어가 엔진·OS와 무관한가
2. 같은 시드와 같은 입력이면 결과가 완전히 같은가 (결정론)
3. 상수가 코드에 하드코딩되지 않고 데이터 파일에서 오는가
4. 변수와 파라미터에 단위가 명시되어 있는가
5. 보존 법칙 테스트가 통과하는가 (예: 질소 총량 수지)
6. 시나리오 그래프가 기대한 방향으로 움직이는가
7. 명세 문서가 코드와 함께 갱신되었는가

## 빌드와 실행 (Windows, PowerShell)

```powershell
cmake --preset vs2022                 # 구성 (처음 한 번, 의존성 내려받기 포함)
cmake --build --preset debug          # 빌드
ctest --preset debug                  # 전체 테스트
build\vs2022\tests\Debug\sim-tests.exe "[determinism]"   # 태그별 실행

# 헤드리스 1년 실행 → CSV → 그래프
build\vs2022\sim-cli\Debug\sim-cli.exe --params data/params.toml --scenario data/scenarios/empty.toml --ticks 8760 --out runs/empty.csv
.venv\Scripts\python.exe tools/plot_csv.py runs/empty.csv --out runs/empty.png
```

- **1작기 회귀**(M5부터 프로토타입 차수·구현 단계마다): `.venv\Scripts\python.exe tools/season_regression.py [--params <파일>]` — M4 `season_*` 7개를 돌려 `tests/baselines/m4_season_summary.json`과 비교해요(누적 ±3%, 월별 ±5%, 당도·경도 ±3%, 첫 수확 ±3일). 기준값 갱신(`--write-baseline`)은 디렉터 승인 후에만.
- 파라미터 스윕: `.venv\Scripts\python.exe tools/param_sweep.py --name <이름> --set "section.key=v1;v2"` (결과는 `runs/sweep/`).
- CI와 같은 조건(Release, 경고를 오류로)으로 확인하려면 `ci` 프리셋을 써요: `cmake --preset ci`, `cmake --build --preset ci`, `ctest --preset ci`.
- Python 도구 환경: `python -m venv .venv` 후 `.venv\Scripts\python.exe -m pip install -r tools/requirements.txt`.
- 합성 기상 데이터를 다시 만들 때: `python tools/gen_weather.py data/weather/synthetic_year.csv`.
- **Godot 확장과 엔진 검사**: `cmake --preset godot -DGODOT_EXECUTABLE=<Godot 콘솔 실행 파일>`(또는 환경 변수 `GODOT_BIN`), `cmake --build --preset godot-release`, `ctest --preset godot -R engine`(E1~E10, G1~G3. 실행 파일을 안 주면 건너뛰어요). 점검 화면은 `<Godot 실행 파일> --path game`으로 열어요. 실행 파일 경로는 커밋하는 파일에 적지 않아요.
- `runs/`는 실행 결과 폴더라서 커밋하지 않아요.

## 디렉터리

| 경로 | 내용 |
| --- | --- |
| `docs/plan.md` | 개발 계획서 |
| `docs/spec/` | 모듈별 상세 명세 (구현 전 승인 필요) |
| `docs/decisions/` | 결정 기록(ADR) |
| `data/` | 모든 파라미터 파일 (TOML), 시나리오, 기상 데이터 |
| `references/` | 공개 재배 자료, 토양 적정 범위 등 참고 자료 |
| `sim-core/` | 순수 시뮬레이션 로직 (표준 라이브러리만 의존) |
| `sim-data/` | 데이터 파일 로더 (toml++) |
| `sim-cli/` | 헤드리스 실행기, CSV 출력 |
| `tests/` | Catch2 테스트 |
| `tools/` | Python 도구 (그래프, 합성 기상 생성) |
| `sim-godot/` | GDExtension 래퍼 (`SimWorld`, godot-cpp. `SIM_BUILD_GODOT`일 때만 빌드) |
| `game/` | Godot 프로젝트 (화면 스크립트, 설정, 헤드리스 검사) |
| `cmake/` | 공통 컴파일 옵션, 외부 의존성 |
