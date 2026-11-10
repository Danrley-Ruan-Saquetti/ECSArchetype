oly_add_test(MODULE app NAME loop SOURCES loop.cpp SUPPORT engine/runtime/ecs/tests/support)
oly_add_test(MODULE app NAME systems SOURCES systems.cpp SUPPORT engine/runtime/ecs/tests/support)
oly_add_test(MODULE app NAME pause SOURCES pause.cpp SUPPORT engine/runtime/ecs/tests/support)
oly_add_test(MODULE app NAME recovery SOURCES recovery.cpp SUPPORT engine/runtime/ecs/tests/support)

oly_add_contract_death(MODULE app SOURCE contract_death.cpp CASES add_system_during_frame while_paused_render frame_time_backwards)
