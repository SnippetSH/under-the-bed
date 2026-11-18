# sim-data: 데이터 파일(TOML, CSV)을 읽어 sim-core 구조체로 바꾸는 로더 (ADR-0002)
# 파일 입출력은 여기서만 하고, sim-core는 toml++에 의존하지 않아요.
add_library(sim-data STATIC
  include/simdata/loader.hpp
  src/loader.cpp)

target_include_directories(sim-data PUBLIC include)
target_link_libraries(sim-data
  PUBLIC sim-core
  PRIVATE tomlplusplus::tomlplusplus)
# 예외 없이 parse_result로 오류를 받아요 (ADR-0005 오류 처리)
target_compile_definitions(sim-data PRIVATE TOML_EXCEPTIONS=0)
sim_apply_compile_options(sim-data)
