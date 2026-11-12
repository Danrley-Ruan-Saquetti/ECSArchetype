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
oly_add_test(MODULE ecs NAME query_by_id SOURCES query_by_id.cpp support/alloc_counter.cpp)
oly_add_test(MODULE ecs NAME surface SOURCES surface.cpp)

oly_add_contract_death(MODULE ecs SOURCE contract_death.cpp
  CASES add_during_each get_dead_entity flush_during_each command_after_destroy destroy_during_each_block
        query_by_id_tag_in_read query_by_id_repeated_component query_by_id_unknown_component
        query_by_id_iteration_out_of_order add_during_query_by_id_iteration get_by_id_missing spawn_during_each
        single_empty single_many try_single_many)
