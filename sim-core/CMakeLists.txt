# sim-core: 순수 시뮬레이션 로직. 표준 라이브러리 외 의존성 금지 (CLAUDE.md "아키텍처 경계")
add_library(sim-core STATIC
  include/sim/calendar.hpp
  include/sim/command.hpp
  include/sim/curve.hpp
  include/sim/error.hpp
  include/sim/event.hpp
  include/sim/hash.hpp
  include/sim/params.hpp
  include/sim/processes.hpp
  include/sim/rng.hpp
  include/sim/scenario.hpp
  include/sim/snapshot.hpp
  include/sim/soil.hpp
  include/sim/world.hpp
  src/byte_io.hpp
  src/serialization.cpp
  src/validation.cpp
  src/validation.hpp
  src/world.cpp
  src/processes/aeration.cpp
  src/processes/cation_exchange.cpp
  src/processes/phosphorus.cpp
  src/processes/crop.cpp
  src/processes/leaching.cpp
  src/processes/nitrification.cpp
  src/processes/organic_matter.cpp
  src/processes/soil_chemistry.cpp
  src/processes/soil_water.cpp
  src/processes/temperature.cpp)

target_include_directories(sim-core PUBLIC include PRIVATE src)
target_compile_features(sim-core PUBLIC cxx_std_23)
sim_apply_compile_options(sim-core)
