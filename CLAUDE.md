# CLAUDE.md — 에이전트 공통 규칙

하드코어 2D 농사 시뮬레이션(비닐하우스 딸기, 토양 진단 루프) 프로젝트예요.
전체 계획은 [docs/plan.md](docs/plan.md), 확정된 결정은 [docs/decisions/](docs/decisions/)에 있어요.
이 파일과 계획서가 충돌하면 **계획서와 승인된 ADR이 우선**이고, 충돌을 발견하면 작업을 멈추고 보고해요.

## 역할

- **SeungH (디렉터·리뷰어)**: 명세 확정, 마일스톤 승인, 밸런스 판단, 우선순위 결정
- **AI 에이전트 (구현자)**: 코드, 테스트, 명세 문서 갱신, 시나리오 그래프 생성

## 현재 마일스톤

**M5a 토양 미생물·소독** (M5는 M5a → M5b로 나눠요, [ADR-0012](docs/decisions/0012-m5-split-and-population-index.md)). M5 전체 완료 기준: 소독 후 양쪽 개체군 감소, 고습 시 병 발생 증가 재현, 태양열 소독은 토양 온도·지속 시간이 클수록 사멸량 증가 (계획서 7장). 하우스 습도와 공기 흐름(병해)도 M5 범위예요 (ADR-0011).
M5a 명세(m5-overview, soil-microbes, disinfection)는 2026-11-19 승인, 사전 작업(m5-overview 13장)과 본 프로토타입(14장) 뒤 최종 명세도 2026-11-19 승인([ADR-0013](docs/decisions/0013-m5a-deep-soil-and-cell-variation.md)). **M5a 정식 구현 완료**(2026-11-19, `ctest --preset ci` 472개 통과), 마일스톤 승인 대기. 다음은 **M5b 명세 다듬기**(house-humidity, foliar-disease)예요. M5b(house-humidity, foliar-disease)는 아직 초안이라 코드를 쓰지 않아요.
(M0~M3는 2026-11-18, M4는 2026-11-19 승인. 단, GitHub Actions CI는 원격 저장소가 없어 실행 검증을 건너뜀)
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
| `cmake/` | 공통 컴파일 옵션, 외부 의존성 |
