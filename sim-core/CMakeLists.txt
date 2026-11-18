# sim-core: 순수 시뮬레이션 로직. 표준 라이브러리 외 의존성 금지 (CLAUDE.md "아키텍처 경계")
add_library(sim-core STATIC
  include/sim/calendar.hpp
  include/sim/command.hpp
  include/sim/error.hpp
  include/sim/event.hpp
  include/sim/hash.hpp
  include/sim/params.hpp
  include/sim/rng.hpp
  include/sim/scenario.hpp
  include/sim/snapshot.hpp
  include/sim/world.hpp
  src/byte_io.hpp
  src/serialization.cpp
  src/world.cpp)

target_include_directories(sim-core PUBLIC include PRIVATE src)
target_compile_features(sim-core PUBLIC cxx_std_23)
sim_apply_compile_options(sim-core)
