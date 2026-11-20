# 엔진 검사 E1~E10 (m6-overview 10.1)를 ctest에 등록해요. SIM_BUILD_GODOT(godot 프리셋)일 때만 tests/CMakeLists.txt가 포함해요.
#
# Godot 실행 파일 위치는 저장소에 적지 않아요(CLAUDE.md). 다음 중 하나로 받아요:
#   - 구성할 때 CMake 캐시: cmake --preset godot -DGODOT_EXECUTABLE=<경로>
#   - 테스트할 때 환경 변수 GODOT_BIN
# 둘 다 없으면 엔진 검사는 SKIP(종료 코드 77)으로 끝나요. E8(글자 검사)은 Godot 없이 늘 돌아요.
# 엔진 검사는 같은 game/ 프로젝트(.godot 캐시)를 쓰므로 RESOURCE_LOCK으로 하나씩 돌려요.

set(GODOT_EXECUTABLE "" CACHE FILEPATH "Godot 4.7.2 executable for headless engine checks (or set GODOT_BIN)")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(SIM_ENGINE_CHECK_OUT "${CMAKE_BINARY_DIR}/engine-checks")
set(SIM_GODOT_ARG "")
if(GODOT_EXECUTABLE)
  set(SIM_GODOT_ARG --godot "${GODOT_EXECUTABLE}")
endif()

# name: ctest 이름(ASCII), script: game/tests/의 파일, ARGN: 검사 스크립트 인자(key=value)
function(sim_add_engine_check name script)
  add_test(NAME ${name}
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/run_engine_check.py"
            --script "res://tests/${script}" ${SIM_GODOT_ARG}
            -- "cli=$<TARGET_FILE:sim-cli>" "out=${SIM_ENGINE_CHECK_OUT}/${name}" ${ARGN})
  set_tests_properties(${name} PROPERTIES
    SKIP_RETURN_CODE 77
    RESOURCE_LOCK godot_project
    LABELS "engine"
    TIMEOUT 1800)
endfunction()

sim_add_engine_check(engine_E1_extension_loads load_check.gd)
sim_add_engine_check(engine_E3_save_roundtrip save_roundtrip.gd)
sim_add_engine_check(engine_E4_command_log_replay replay_check.gd)
sim_add_engine_check(engine_E5_bad_saves_rejected bad_saves.gd)
sim_add_engine_check(engine_E6_command_conversion command_roundtrip.gd)
sim_add_engine_check(engine_E7_batch_size_independent batch_independence.gd)
sim_add_engine_check(engine_E9_snapshot_matches_cli_csv snapshot_vs_csv.gd)
sim_add_engine_check(engine_E10_auto_irrigation_replay auto_irrigation.gd)

# E2: 전체 시나리오 목록(tools/compare_engine_cli.py E2_CASES)을 모든 틱 비교. 결과표를 출력해요.
add_test(NAME engine_E2_matches_cli_every_tick
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/compare_engine_cli.py" --suite
          --cli "$<TARGET_FILE:sim-cli>" --out "${SIM_ENGINE_CHECK_OUT}/engine_E2" ${SIM_GODOT_ARG})
set_tests_properties(engine_E2_matches_cli_every_tick PROPERTIES
  SKIP_RETURN_CODE 77
  RESOURCE_LOCK godot_project
  LABELS "engine"
  TIMEOUT 7200)

# E8: 게임 화면 코드에 get_true_ 호출이 없는지 (Godot 없이)
add_test(NAME engine_E8_no_true_value_access_outside_dev_panel
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_true_access.py")
set_tests_properties(engine_E8_no_true_value_access_outside_dev_panel PROPERTIES LABELS "engine")
