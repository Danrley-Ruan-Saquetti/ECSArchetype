oly_add_test(MODULE app NAME loop SOURCES loop.cpp SUPPORT engine/runtime/ecs/tests/support)
oly_add_test(MODULE app NAME systems SOURCES systems.cpp SUPPORT engine/runtime/ecs/tests/support)
oly_add_test(MODULE app NAME pause SOURCES pause.cpp SUPPORT engine/runtime/ecs/tests/support)
oly_add_test(MODULE app NAME recovery SOURCES recovery.cpp SUPPORT engine/runtime/ecs/tests/support)
oly_add_test(MODULE app NAME events SOURCES events.cpp SUPPORT engine/runtime/ecs/tests/support)
oly_add_test(MODULE app NAME events_loop SOURCES events_loop.cpp SUPPORT engine/runtime/ecs/tests/support)
oly_add_test(MODULE app NAME actions SOURCES actions.cpp SUPPORT engine/runtime/ecs/tests/support)

oly_add_contract_death(MODULE app SOURCE contract_death.cpp CASES add_system_during_frame while_paused_render frame_time_backwards emit_undeclared_event on_undeclared_event declare_event_twice on_during_frame declare_event_during_frame event_round_limit events_wrong_type declare_action_during_frame)
