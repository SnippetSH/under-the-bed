# 외부 의존성. 버전은 태그로 고정해요.
include(FetchContent)

# toml++ — 데이터 파일 파싱 (ADR-0002). sim-data에서만 써요.
FetchContent_Declare(tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0
  GIT_SHALLOW    TRUE
  SYSTEM)
FetchContent_MakeAvailable(tomlplusplus)

# Catch2 v3 — 테스트 (ADR-0003)
if(SIM_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
    SYSTEM)
  FetchContent_MakeAvailable(Catch2)
  # include()는 새 스코프를 만들지 않아서, 여기서 추가한 경로가 tests/에서도 보여요.
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

# godot-cpp 10.0.0-stable — GDExtension 바인딩 (ADR-0015). SIM_BUILD_GODOT일 때만.
if(SIM_BUILD_GODOT)
  # godot-cpp 기본값(/MT, _HAS_EXCEPTIONS=0)이 우리 타깃에 번지지 않게 먼저 정해요 (ADR-0015 결정 5).
  set(GODOTCPP_API_VERSION "4.7" CACHE STRING "Godot API version for godot-cpp" FORCE)
  set(GODOTCPP_DISABLE_EXCEPTIONS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(godot-cpp
    GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
    GIT_TAG        10.0.0-stable
    GIT_SHALLOW    TRUE
    SYSTEM)
  FetchContent_MakeAvailable(godot-cpp)
endif()
