// Forwards the node environment to the program's getenv (OLY_FUZZ_SEED, OLY_FUZZ_OPS).
Module.preRun = [() => Object.assign(ENV, process.env)];
