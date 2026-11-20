# sim-godot: GDExtension 래퍼 (m6-overview 3·4장, ADR-0015). 변환만 하고 계산은 하지 않아요.
# 빌드 설정의 근거는 ADR-0015 "프로토타입 확인 결과"에 있어요:
#   - MSVC 런타임은 최상위 CMakeLists.txt에서 /MD로 명시해요 (안 하면 godot-cpp가 코어까지 /MT로 바꿔요, U2)
#   - godot-cpp의 lib·pdb 출력 폴더를 구성별로 나눠요 (U3)
#   - 엔진 캐시의 확장 목록(game/.godot/extension_list.cfg)을 빌드가 써요 (U4)

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
  src/snapshot_codec.cpp
  src/text.hpp)

target_link_libraries(sim-godot PRIVATE sim-core sim-data godot-cpp)
sim_apply_compile_options(sim-godot)

set(SIM_GODOT_GAME_DIR "${PROJECT_SOURCE_DIR}/game" CACHE INTERNAL "Godot project directory")

# 엔진이 읽는 위치(game/bin/)로 DLL을 복사해요. 커밋하지 않아요.
# Debug·Release DLL은 이름이 같아서 마지막에 빌드한 구성이 game/bin에 남아요 (U3, 완료 판정은 Release, 결정 F5).
# 새로 체크아웃한 뒤 --import 없이 헤드리스로 바로 확장을 읽도록 엔진 캐시의 확장 목록을 미리 써 둬요 (U4).
# 편집기가 처음 열 때 쓰는 내용과 같아요.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/extension_list.cfg" "res://under_the_bed.gdextension\n")
add_custom_command(TARGET sim-godot POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_GODOT_GAME_DIR}/.godot"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/extension_list.cfg"
          "${SIM_GODOT_GAME_DIR}/.godot/extension_list.cfg"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${SIM_GODOT_GAME_DIR}/bin"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:sim-godot>" "${SIM_GODOT_GAME_DIR}/bin/sim-godot.dll"
  VERBATIM)

# data/ 원본을 game/data/로 복사해요 (구현자 판단 B3, 커밋하지 않아요). DLL이 다시 빌드되지 않아도
# 데이터가 바뀌면 따라가도록 빌드할 때마다 돌려요(파일이 작아서 빨라요).
add_custom_target(sim-godot-data ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/data" "${SIM_GODOT_GAME_DIR}/data"
  # 편집기가 data/의 CSV를 번역 자원으로 가져오지 않게 해요 (FileAccess로는 그대로 읽혀요).
  COMMAND ${CMAKE_COMMAND} -E touch "${SIM_GODOT_GAME_DIR}/data/.gdignore"
  COMMENT "Copying data/ to game/data/"
  VERBATIM)
add_dependencies(sim-godot sim-godot-data)
