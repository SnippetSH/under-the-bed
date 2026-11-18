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
