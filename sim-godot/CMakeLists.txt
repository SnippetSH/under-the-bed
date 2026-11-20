# sim-godot: GDExtension 래퍼 (m6-overview 4장, ADR-0015). 변환만 하고 계산은 하지 않아요.
# 프로토타입(proto/m6-check) 코드예요.

# godot-cpp 산출물 위치를 구성별로 나눠요. 기본값은 "$<1:bin>"이라 Debug·Release가 같은 .lib를 덮어써요 (U3).
set_target_properties(godot-cpp PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/godot-cpp-lib/$<CONFIG>"
  PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/godot-cpp-lib/$<CONFIG>")

add_library(sim-godot SHARED
  src/register_types.cpp
  src/sim_world.hpp
  src/sim_world.cpp
  src/command_codec.hpp
  src/command_codec.cpp
  src/snapshot_codec.hpp
  src/snapshot_codec.cpp)

target_link_libraries(sim-godot PRIVATE sim-core sim-data godot-cpp)
sim_apply_compile_options(sim-godot)

# 엔진이 읽는 위치(game/bin/)로 복사해요. 커밋하지 않아요.
set(SIM_GODOT_GAME_DIR "${PROJECT_SOURCE_DIR}/game")
# 새로 체크아웃한 뒤 --import 없이 헤드리스로 바로 확장을 읽도록 엔진 캐시의 확장 목록을 미리 써 둬요 (P0 확인 사항).
# 편집기가 처음 열 때 쓰는 내용과 같아요.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/extension_list.cfg" "res://under_the_bed.gdextension\n")
add_custom_command(TARGET sim-godot POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_GODOT_GAME_DIR}/.godot"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/extension_list.cfg"
          "${SIM_GODOT_GAME_DIR}/.godot/extension_list.cfg"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_GODOT_GAME_DIR}/bin"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:sim-godot>" "${SIM_GODOT_GAME_DIR}/bin/sim-godot.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/data" "${SIM_GODOT_GAME_DIR}/data"
  # 편집기가 data/의 CSV를 번역 자원으로 가져오지 않게 해요 (FileAccess로는 그대로 읽혀요).
  COMMAND ${CMAKE_COMMAND} -E touch "${SIM_GODOT_GAME_DIR}/data/.gdignore"
  VERBATIM)

# 같은 컴파일 옵션으로 만든 "CLI 쪽" 부동소수점 상태 확인용 실행 파일 (R2/U5, 프로토타입 전용)
add_executable(fp-probe src/fp_probe.cpp)
sim_apply_compile_options(fp-probe)
