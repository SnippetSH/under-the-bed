# 테스트 (ADR-0003). 태그: [unit] [determinism] [m0] ...
add_executable(sim-tests
  test_helpers.hpp
  test_rng.cpp
  test_calendar.cpp
  test_world.cpp
  test_serialization.cpp
  test_determinism.cpp
  test_loader.cpp)

target_link_libraries(sim-tests PRIVATE sim-core sim-data Catch2::Catch2WithMain)
# 실제 data/ 파일을 읽는 테스트용 저장소 경로
target_compile_definitions(sim-tests PRIVATE SIM_REPO_DIR="${PROJECT_SOURCE_DIR}")
sim_apply_compile_options(sim-tests)

include(Catch)
catch_discover_tests(sim-tests DISCOVERY_MODE PRE_TEST)
