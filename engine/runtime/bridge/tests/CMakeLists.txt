oly_add_test(MODULE bridge NAME c_api SOURCES c_api.cpp test_schema_registry.cpp
  SUPPORT engine/runtime/ecs/tests/support)

oly_add_test(MODULE bridge NAME engine_api SOURCES engine_api.cpp test_schema_registry.cpp
  SUPPORT engine/runtime/ecs/tests/support)

oly_add_contract_death(MODULE bridge SOURCE contract_death.cpp CASES query_end_out_of_order system_spawn_outside_system engine_phase_out_of_range)
