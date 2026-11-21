# Instructions for agents

This repository is managed by Apron, and Apron is built with itself. The full protocol is in [`docs/agent-protocol.md`](docs/agent-protocol.md); read it once. In short:

1. Build: `cmake -S . -B build -G Ninja && cmake --build build`.
2. `./build/apron run start` lists the tasks ready to pick. Do not read all of `.apron/tasks.md`: it is the project's history and costs context.
3. `./build/apron run start <id> --agent <your-name> --intent "<one sentence>"` prints your task: scope, done-when, decision, and `review`, a person's comment on an earlier run that comes first. If it warns that your scope meets a run in progress, ask before going on.
4. `./build/apron report step "<what changed>"` on each change of phase; `report ask` for a person's decision (also when staying inside the `scope` would force a worse design); `report blocked` when something outside stops you; `report note` for what the next run should know.
5. `./build/apron check`, then `./build/apron run finish --result done "<what was done; what was left out; what to look at first>"`.
6. Commit on the task's branch with `tasks.md` included and the trailer `Apron-Run: <run id>`.

Code: C++20 with the standard library; the one dependency is libghostty-vt for the agents' terminals (the build fetches it and needs `zig` 0.17). Code and comments in English. Every string the interface shows goes through `tr()` or `trf()`, with its Portuguese entry in `src/core/i18n.cpp`; a test fails if one is missing.
