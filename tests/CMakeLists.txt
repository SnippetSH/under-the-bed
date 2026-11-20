# 테스트 (ADR-0003). 태그: [unit] [determinism] [conservation] [scenario] [extreme] [m0] [m1] ...
add_executable(sim-tests
  test_helpers.hpp
  test_rng.cpp
  test_calendar.cpp
  test_curve.cpp
  test_world.cpp
  test_serialization.cpp
  test_determinism.cpp
  test_loader.cpp
  test_soil_water.cpp
  test_temperature.cpp
  test_organic_matter.cpp
  test_nitrification.cpp
  test_m1_scenarios.cpp
  test_m1_conservation.cpp
  test_m1_determinism.cpp
  test_aeration.cpp
  test_soil_chemistry.cpp
  test_crop.cpp
  test_validation.cpp
  test_m2_scenarios.cpp
  test_m2_conservation.cpp
  test_m2_determinism.cpp
  test_cation_exchange.cpp
  test_phosphorus.cpp
  test_manganese.cpp
  test_m3_scenarios.cpp
  test_m3_conservation.cpp
  test_m3_determinism.cpp
  test_house_climate.cpp
  test_phenology.cpp
  test_fruit.cpp
  test_denitrification.cpp
  test_m4_scenarios.cpp
  test_m4_conservation.cpp
  test_m4_determinism.cpp
  test_m5a_prework.cpp
  test_microbes.cpp
  test_disinfection.cpp
  test_soil_disease.cpp
  test_m5a_variation.cpp
  test_m5a_scenarios.cpp
  test_m5a_conservation.cpp
  test_m5a_determinism.cpp
  test_house_humidity.cpp
  test_foliar_disease.cpp
  test_m5b_foliar_world.cpp
  test_m5b_mite_rates.cpp
  m5b_world_checks.hpp
  test_m5b_scenarios.cpp
  test_m5b_determinism.cpp
  test_m6_core_api.cpp
  test_m6_schedule_parse.cpp)

target_link_libraries(sim-tests PRIVATE sim-core sim-data Catch2::Catch2WithMain)
# 실제 data/ 파일을 읽는 테스트용 저장소 경로
target_compile_definitions(sim-tests PRIVATE SIM_REPO_DIR="${PROJECT_SOURCE_DIR}")
sim_apply_compile_options(sim-tests)

include(Catch)
catch_discover_tests(sim-tests DISCOVERY_MODE PRE_TEST)

# M6a 엔진 검사 E1~E10 (godot 프리셋에서만)
if(SIM_BUILD_GODOT)
  include(${CMAKE_CURRENT_SOURCE_DIR}/GodotTests.cmake)
endif()
