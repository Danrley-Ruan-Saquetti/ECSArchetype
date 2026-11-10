// Repassa o ambiente do node ao getenv do programa (ECS_FUZZ_SEED, ECS_FUZZ_OPS).
Module.preRun = [() => Object.assign(ENV, process.env)];
