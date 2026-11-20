# ADR-0015: Godot·godot-cpp 버전 고정과 GDExtension 빌드 방식

- 상태: 승인 (2026-11-20, m6-overview 결정 D9)
- 날짜: 2026-11-20
- 관련: [ADR-0001](0001-game-engine.md)(Godot 4 + GDExtension), [ADR-0004](0004-determinism-scope.md) 규칙 3(코어는 한 번만 빌드), [ADR-0005](0005-cpp-toolchain.md)(툴체인), [m6-overview](../spec/m6-overview.md)

## 맥락

ADR-0001은 "Godot와 godot-cpp 버전은 뷰어 작업을 시작할 때 그 시점의 최신 4.x 안정판으로 고정하고, 이 ADR에 버전을 덧붙여요"라고 했어요. M6(엔진 연결)를 시작하므로 지금 정해요. 함께 정해야 하는 것이 있어요.

- ADR-0004 규칙 3은 "GDExtension도 CMake로 빌드해서 컴파일 옵션이 갈라지지 않게"를 요구해요. godot-cpp는 SCons가 기본이고 CMake는 "지원하지만 보조(secondary)"예요(공식 문서, 2026-11-20 확인).
- godot-cpp의 CMake는 **기본값으로** 우리 빌드 옵션을 바꿀 수 있어요(아래 "확인한 사실" 3·4). 그대로 붙이면 `sim-core`·`sim-cli`·테스트의 런타임 라이브러리 설정이 조용히 바뀌어요.

### 확인한 사실 (2026-11-20)

| # | 사실 | 확인 방법 |
| --- | --- | --- |
| 1 | 최신 4.x 안정판은 **Godot 4.7.2** (2026-08-18). 4.8은 베타 | godotengine.org 내려받기 보관 목록 |
| 2 | 이 PC에 Godot 4.7.2 표준(비 .NET) 빌드가 있어요: `--version` = `4.7.2.stable.official.ed1daf0bf`. PATH에는 없어요. 저장소와 이 PC에 godot-cpp는 없어요(`build/*/_deps`에는 toml++·Catch2뿐) | 디렉터가 설치, 콘솔 실행 파일로 확인. 저장소·빌드 폴더 검색 |
| 3 | godot-cpp는 **10.x부터 Godot와 따로 버전을 매겨요.** 대상 엔진은 `api_version`(CMake: `GODOTCPP_API_VERSION`, 값 4.3~4.8)으로 골라요. 태그 `10.0.0-stable`(2026-09-15)이 있어요. "이전 버전 대상 확장은 이후 마이너 버전에서 동작, 반대는 안 됨" | godot-cpp README, 태그 목록, `cmake/godotcpp.cmake` |
| 4 | godot-cpp CMake 기본값: `GODOTCPP_USE_STATIC_CPP=ON` → `CMAKE_MSVC_RUNTIME_LIBRARY`가 비어 있으면 **`MultiThreaded`(/MT)** 로 캐시에 설정. `GODOTCPP_DISABLE_EXCEPTIONS=ON` → MSVC에서 **`_HAS_EXCEPTIONS=0`을 PUBLIC**으로 전파. `/utf-8`, `/Zc:__cplusplus`, `/MP`도 PUBLIC. `/fp` 관련 옵션은 없음. C++17, 타깃 이름 `godot-cpp`(별칭 `godot::cpp`), `GODOTCPP_TARGET` 기본 `template_debug` | godot-cpp `cmake/windows.cmake`, `cmake/common_compiler_flags.cmake`, `cmake/godotcpp.cmake` (master) |
| 5 | godot-cpp 공식 템플릿은 `add_subdirectory(godot-cpp SYSTEM)` + `target_link_libraries(... PRIVATE godot-cpp)`, Python 3과 git이 필요, CMake 3.25 이상 | godot-cpp-template `CMakeLists.txt` |
| 6 | 헤드리스 실행: `--headless`, `-s/--script`, `--path`, `--quit`, `--quit-after`, `--fixed-fps`, `--import`, `--export-release`, 사용자 인자 구분자 `--`/`++`(`OS.get_cmdline_user_args()`) | 이 PC의 4.7.2 `--help`, 공식 문서(4.7) |

## 결정 (제안)

1. **엔진 버전: Godot 4.7.2 안정판(표준 빌드, .NET 아님)** 으로 고정해요. 패치 버전(4.7.x)까지 고정하고, 올릴 때는 별도 PR로 "엔진 일치 검사"(m6-overview 10장)를 다시 돌려요.
2. **godot-cpp: 태그 `10.0.0-stable`, `GODOTCPP_API_VERSION=4.7`** 로 고정해요. 가져오기는 기존 의존성(toml++, Catch2)과 같은 **CMake `FetchContent` + 태그 고정**이에요(`cmake/SimDependencies.cmake`). 서브모듈은 쓰지 않아요.
   - **확인 필요**: `10.0.0-stable` 태그가 API 4.7을 담고 있는지(godot-cpp master에는 4.3~4.8 목록이 있지만 태그 시점 내용은 확인하지 못했어요), FetchContent로 가져왔을 때 `SYSTEM` 포함과 타깃 생성이 템플릿의 `add_subdirectory`와 같게 동작하는지. 프로토타입 P0에서 확인하고, 안 되면 커밋 해시 고정으로 바꿔요.
3. **CMake 한 트리에서 같이 빌드해요 (ADR-0004 규칙 3 유지).** 새 타깃 `sim-godot`(공유 라이브러리)이 `sim-core`·`sim-data`·`godot-cpp`를 링크해요. `sim-core` 타깃은 지금 것 하나 그대로이고, 같은 빌드 트리의 `sim-cli`·`sim-tests`·`sim-godot`이 같은 `sim-core.lib`를 링크해요.
4. **켜는 스위치와 프리셋.** 최상위 옵션 `SIM_BUILD_GODOT`(기본 OFF)을 두고, 프리셋 `godot`(구성)·`godot-debug`/`godot-release`(빌드)·`godot`(테스트)을 추가해요. 기존 `vs2022`·`ci` 프리셋은 그대로라서 엔진 없이도 지금처럼 빌드·테스트돼요.
   - "CLI와 같은 결과" 검사는 **항상 `godot` 프리셋 트리 안의 `sim-cli`와 `sim-godot`** 끼리 해요(같은 구성끼리: Release 대 Release).
5. **godot-cpp 기본값을 우리 쪽에서 덮어써요** (사실 4).
   - `CMAKE_MSVC_RUNTIME_LIBRARY`를 최상위 `CMakeLists.txt`에서 CMake 기본값(`MultiThreaded$<$<CONFIG:Debug>:Debug>DLL`, 즉 지금과 같은 /MD·/MDd)으로 **명시**해요. godot-cpp보다 먼저 설정해서 기존 타깃의 설정이 바뀌지 않게 해요.
   - `GODOTCPP_DISABLE_EXCEPTIONS=OFF`. `sim-godot`이 `sim-core` 헤더(`std::expected`, `std::vector`, `std::string`)를 `_HAS_EXCEPTIONS=0`으로 다르게 컴파일하지 않게 해요.
   - godot-cpp 헤더는 SYSTEM 포함으로 받아 우리 경고 옵션(/W4, CI의 /WX)이 걸리지 않게 해요.
   - `sim-godot`에도 `sim_apply_compile_options`를 적용해요(/fp:precise 포함).
   - **확인 필요**: 위 설정으로 godot-cpp(C++17)와 `sim-godot`(C++23)이 MSVC 19.38+에서 함께 빌드되는지, Godot 본체(자체 런타임)와 /MD 확장 DLL이 문제없이 로드되는지. P0에서 확인해요.
6. **Godot 실행 파일 위치는 저장소에 적지 않아요.** CMake 캐시 변수 `GODOT_EXECUTABLE`(없으면 환경 변수 `GODOT_BIN`)로 받아요. 값이 없으면 `sim-godot` 빌드는 되고 엔진 테스트만 등록하지 않아요(구성 메시지로 알림). 각자 `CMakeUserPresets.json`(이미 .gitignore에 있음)이나 환경 변수에 적어요. 이 PC의 예: `B:\Tools\Godot\Godot_v4.7.2-stable_win64_console.exe`.
   - 엔진 테스트는 시작할 때 `--version`을 읽어 `4.7.2`가 아니면 실패해요(다른 버전으로 조용히 돌지 않게).
7. **언어 경계.** ADR-0001 그대로예요: 바인딩(`sim-godot`)은 C++, 화면·입력·세이브 파일 관리는 GDScript, 시뮬레이션 로직은 엔진 쪽에 한 줄도 두지 않아요.

### 승인되면 함께 적용할 문서 변경 (초안)

- ADR-0001 맨 아래에 한 줄 덧붙임(ADR-0001이 스스로 정한 절차): "**버전 고정 (2026-11-20, ADR-0015)**: Godot 4.7.2 안정판(표준 빌드), godot-cpp 10.0.0-stable(`GODOTCPP_API_VERSION=4.7`)."
- `CLAUDE.md` "빌드와 실행"에 `godot` 프리셋과 `GODOT_EXECUTABLE` 설명 추가(M6 구현 PR에서).
- `.gitignore`의 `*.import`: Godot 4 공식 권장은 `.godot/`만 무시하고 `*.import`는 커밋하는 것으로 알고 있어요(**확인 필요**, 이번에 다시 확인하지 못했어요). M6 구현 PR에서 확인 후 고쳐요.

## 근거

- 4.7.2는 ADR-0001의 기준("그 시점의 최신 4.x 안정판")에 맞고 이미 이 PC에 있어요. 4.8은 베타라 제외해요.
- godot-cpp 10.x는 엔진과 버전이 분리돼서, 엔진 패치 버전을 올려도 바인딩을 다시 고정할 필요가 줄어요. API 4.7 대상이면 4.8 이후에서도 로드돼요(사실 3).
- 한 트리 빌드는 `sim-core`의 목적 코드가 CLI와 확장에서 **같은 .lib**임을 보장해요. /MT 전파를 막아 두면 기존 565개 테스트와 1작기 기준값이 M6 때문에 달라질 이유가 없어요.
- `SIM_BUILD_GODOT` 기본 OFF: godot-cpp는 바인딩 수천 개를 생성·컴파일해서 처음 빌드가 길고 Python이 필요해요. 코어만 고치는 작업(밸런싱)에는 부담을 주지 않아요.

## 결과 (좋은 점, 감수하는 점)

- 좋은 점: 엔진이 없어도 코어 작업은 그대로예요. 엔진 버전·바인딩 버전·빌드 옵션이 한 문서에 고정돼요.
- 감수하는 점: `godot` 프리셋 트리에서 `sim-core`를 한 번 더 컴파일해요(옵션은 같아요). ADR-0004 규칙 3의 "한 번만"은 **"한 빌드 트리 안에서 타깃 하나"** 로 읽어요. 두 트리의 결과가 같은지는 `godot` 트리에서도 `ctest`와 1작기 회귀를 돌려 확인해요.
- 감수하는 점: godot-cpp의 CMake는 보조 빌드 시스템이라 SCons보다 기능이 늦을 수 있어요. 막히면 아래 대안 1로 물러나요(그 경우 새 ADR).
- 감수하는 점: ADR-0004의 보장은 "같은 바이너리"예요. CLI(.exe)와 확장(.dll)은 같은 `sim-core.lib`를 쓰지만 프로세스가 달라요. Godot 프로세스가 부동소수점 제어 상태(MXCSR)를 바꿔 두면 결과가 달라질 수 있어요. 틱별 해시 비교(m6-overview 10장 E2)로 잡고, 발견되면 `sim-godot`이 `step` 앞뒤로 상태를 맞춰요(코어는 건드리지 않아요).

## 검토한 대안

1. **godot-cpp만 SCons로 미리 빌드하고 `.lib`를 CMake에서 가져오기**: 공식 기본 경로라 안정적이지만 빌드 도구가 둘이 되고, 런타임 라이브러리(/MT 기본)를 SCons 쪽에서 따로 맞춰야 해요. CMake 경로가 막힐 때의 대비책이에요.
2. **`sim-core`를 SCons로도 빌드**: ADR-0004 규칙 3 위반(옵션이 갈라져요). 제외.
3. **C ABI 래퍼 + GDScript에서 직접 호출**: GDScript에는 일반 FFI가 없어서 결국 GDExtension이 필요해요. 제외.
4. **`SIM_BUILD_GODOT`을 기본 ON**: 빌드 트리가 하나라 단순하지만 모든 빌드가 godot-cpp와 Python에 묶여요. 엔진 작업이 일상이 되는 M7에서 다시 판단해요.
5. **Godot 4.8 베타로 시작**: 안정판이 아니에요. 제외.
6. **godot-cpp를 git 서브모듈로**: 템플릿의 방식이지만 저장소에 이미 FetchContent 관례가 있고, 서브모듈은 체크아웃 절차가 늘어요. 제외.

## 프로토타입 확인 결과 (2026-11-20, `proto/m6-check`)

- godot-cpp `10.0.0-stable`은 API 4.3~4.7을 담고 FetchContent로 붙어요. Godot 4.7.2에서 확장이 로드돼요.
- `CMAKE_MSVC_RUNTIME_LIBRARY`를 /MD로 명시해야 해요. 명시하지 않으면 godot-cpp가 `sim-core`까지 /MT로 바꿔요. 명시한 뒤 기존 테스트와 1작기 회귀는 그대로예요.
- godot-cpp의 lib·pdb 출력 폴더를 구성(Debug/Release)별로 덮어써야 해요. Debug와 Release DLL은 이름이 같아요.
- 헤드리스 첫 실행 전에 `game/.godot/extension_list.cfg`가 필요해서 빌드가 그 파일을 써요. 첫 `--import`는 가져오기를 끝낸 뒤 종료할 때 엔진이 크래시해요(감수).
- Godot 프로세스 안에서도 `season_standard`의 모든 틱 해시가 CLI와 같아요.
