oly_add_test(MODULE ecs NAME unit_table SOURCES unit_table.cpp)
oly_add_test(MODULE ecs NAME unit_type_info SOURCES unit_type_info.cpp)
oly_add_test(MODULE ecs NAME unit_entity SOURCES unit_entity.cpp)
oly_add_test(MODULE ecs NAME unit_archetype SOURCES unit_archetype.cpp)
oly_add_test(MODULE ecs NAME unit_world SOURCES unit_world.cpp support/alloc_counter.cpp)
oly_add_test(MODULE ecs NAME invariants SOURCES invariants.cpp)
oly_add_test(MODULE ecs NAME fuzz_vs_model SOURCES fuzz_vs_model.cpp)
oly_add_test(MODULE ecs NAME nontrivial SOURCES nontrivial.cpp)
oly_add_test(MODULE ecs NAME queries SOURCES queries.cpp)
oly_add_test(MODULE ecs NAME command_buffer SOURCES command_buffer.cpp)
oly_add_test(MODULE ecs NAME raw_query SOURCES raw_query.cpp)

oly_add_contract_death(MODULE ecs SOURCE contract_death.cpp
  CASES add_during_each get_dead_entity flush_during_each command_after_destroy destroy_during_each_block
        raw_query_tag_in_read raw_query_repeated_component raw_query_unknown_component
        raw_query_end_without_begin add_during_raw_iteration)
