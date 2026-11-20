# 엔진 검사 E1~E10 (m6-overview 10.1), 게임 스크립트 검사 G1~G3, 캐릭터 화면 검사 C1~C14 (m6b-character-play 9.1)를 ctest에 등록해요. SIM_BUILD_GODOT(godot 프리셋)일 때만 tests/CMakeLists.txt가 포함해요.
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

# G1~G3: 게임 스크립트(game/scripts/) 수준의 검사. 진짜 SimRunner·SaveStore·화면 장면을 써요.
sim_add_engine_check(engine_G1_game_save_store game_save_store.gd)   # 세이브 묶음·칸·자동 저장·버전 거부·깨진 파일 (E3·E5 게임 수준)
sim_add_engine_check(engine_G2_game_runner game_runner.gd)          # 자동 물 주기(E10·H9), 작기 종료, 263일 해시 = sim-cli (H6)
sim_add_engine_check(engine_G3_game_screen game_screen.gd)          # 행동 패널 17종, 거부 알림, 개발자 패널 (H2·H3·H8, F2·F4)

# C1~C14 (m6b-character-play 9.1): 캐릭터 화면 검사. 프로토타입 검사(p5~p18)를 정리한 것이에요. C7은 아래(Godot 없이).
sim_add_engine_check(engine_C01_target_cell_rule c01_target_cell.gd)                  # 대상 칸 규칙, 80칸·장치 8종 서는 자리
sim_add_engine_check(engine_C02_tool_to_command c02_tool_commands.gd)                 # 도구 11종 → 1×1 명령, 장치 창, 행동 표시
sim_add_engine_check(engine_C03_hold_and_walk_each_cell_once c03_hold_walk.gd)        # 누른 채 걷기·달리기: 칸마다 한 번
sim_add_engine_check(engine_C04_character_play_matches_cli c04_play_replays_on_cli.gd) # 캐릭터 판 기록 → sim-cli, 모든 틱 해시
sim_add_engine_check(engine_C05_work_speed_independent c05_speed_independent.gd)      # 일하는 속도 3가지 → 같은 최종 해시
sim_add_engine_check(engine_C06_character_save_roundtrip c06_save_roundtrip.gd)       # 저장 왕복: 해시·위치·도구·대기 행동, 자는 동안 저장 안 함
sim_add_engine_check(engine_C08_cancel_before_core c08_cancel.gd)                     # Z·Shift+Z·정식 미리 보기, 자동 물 주기는 남음
sim_add_engine_check(engine_C09_rejection_notice_korean c09_rejections.gd)            # 누른 즉시 한국어 거부 알림
sim_add_engine_check(engine_C10_cell_info c10_cell_info.gd)                           # 칸 정보 = 스냅샷, "꽃·꽃봉오리"
sim_add_engine_check(engine_C11_help c11_help.gd)                                     # 도움말 12쪽, 항목 목록, 창 크기 고정
sim_add_engine_check(engine_C12_toolbar c12_toolbar.gd)                               # 도구 줄 숨김·잠깐 보이기·Tab (UI 비례 크기는 아직: 파일 안 TODO)
sim_add_engine_check(engine_C13_model_slots c13_model_slots.gd)                       # 모델 자리, character.glb, 평면 그림으로 돌아가기
sim_add_engine_check(engine_C14_cover_fans_outside c14_cover_fans_outside.gd)         # 피복 겹·측창·유동팬 5개·바깥 땅과 하늘

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

# C7 (m6b 9.1): E8을 캐릭터 화면에도. char_play 장면에서 이어지는 파일 전부를 따라가 get_true_ 글자를 찾아요 (Godot 없이)
add_test(NAME engine_C07_no_true_value_access_in_character_screen
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_true_access.py" --scene res://scenes/char_play.tscn)
set_tests_properties(engine_C07_no_true_value_access_in_character_screen PROPERTIES LABELS "engine")
