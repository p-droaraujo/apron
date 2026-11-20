# Instructions for agents

This repository is managed by Apron, and Apron is built with itself. The full protocol is in [`docs/agent-protocol.md`](docs/agent-protocol.md); read it once. In short:

1. Build: `cmake -S . -B build -G Ninja && cmake --build build`.
2. Pick a `[ ]` task in `.apron/tasks.md` whose `depends` are `[x]`. A `review` field is a person's comment on an earlier run: it comes first.
3. `./build/apron run start <id> --agent <your-name> --intent "<one sentence>"`. If it warns that your scope meets a run in progress, ask before going on.
4. `./build/apron report step "<what changed>"` on each change of phase; `report ask` for a person's decision (also when staying inside the `scope` would force a worse design); `report blocked` when something outside stops you; `report note` for what the next run should know.
5. `./build/apron check`, then `./build/apron run finish --result done "<what was done; what was left out; what to look at first>"`.
6. Commit on the task's branch with `tasks.md` included and the trailer `Apron-Run: <run id>`.

Code: C++20 with the standard library; the one dependency is libghostty-vt for the agents' terminals (the build fetches it and needs `zig` 0.17). Code and comments in English. Every string the interface shows goes through `tr()` or `trf()`, with its Portuguese entry in `src/core/i18n.cpp`; a test fails if one is missing.
