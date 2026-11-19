# Instructions for agents

This repository is managed by Apron, and Apron is built with itself. Follow the protocol in `docs/agent-protocol.md`; this is the short version.

## Before you start

1. Read `.apron/tasks.md` and pick a `[ ]` task whose dependencies are `[x]` (`./build/apron tasks --ready` lists them).
2. Build the CLI: `cmake -S . -B build -G Ninja && cmake --build build`.
3. Start the run: `./build/apron run start <id> --agent <your-name> --intent "<what you will do, in one sentence>"`.

## While you work

- On each change of phase (read, plan, edit, test, fix): `./build/apron report step "<what changed>"`.
- Need a decision from the person (business rules, customer-facing, architecture, scope)? `./build/apron report ask "<the question>"` and wait.
- Something outside stops you? `./build/apron report blocked --cause <gpu|budget|lock|machine|other> "<the reason>"`.
- Learned something the next run should know? `./build/apron report note "<the lesson>"`.
- Stay inside the task's `scope`. Need to leave it, or would staying inside force a worse design? That is an `ask`.
- Write events in English, the language of this project.

## When you finish

1. Run the checks: `./build/apron check`.
2. Close the run: `./build/apron run finish --result done "<what was done; what was left out; what to look at first>"`. This marks the task `[x]` in `tasks.md`.
3. Commit on the task's branch, with `tasks.md` included and the trailer `Apron-Run: <run id>`.

One sentence per event, present tense, saying what changed. Apron measures files, time and cost on its own: do not report them.

## Code conventions

- C++20, no external dependencies in the core. Code, comments and commit messages in English.
- Every string the interface shows goes through `tr()` or `trf()` in English, with its Portuguese entry in `src/core/i18n.cpp`. A test fails if one is missing.
