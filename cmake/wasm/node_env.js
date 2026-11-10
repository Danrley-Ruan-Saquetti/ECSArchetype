// Repassa o ambiente do node ao getenv do programa (OLY_FUZZ_SEED, OLY_FUZZ_OPS).
Module.preRun = [() => Object.assign(ENV, process.env)];
