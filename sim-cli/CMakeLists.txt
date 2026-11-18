# sim-cli: 시나리오를 헤드리스로 실행하고 틱별 상태를 CSV로 출력
add_executable(sim-cli src/main.cpp)
target_link_libraries(sim-cli PRIVATE sim-core sim-data)
sim_apply_compile_options(sim-cli)
