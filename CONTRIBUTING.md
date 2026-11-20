# Contributing

Thanks for looking. Apron is small on purpose; the best contributions keep it that way.

## Issues

Bugs, rough edges and ideas are all welcome as issues. For a bug, say what you ran, on which system (Linux or macOS, terminal), what you expected and what happened. For an idea, say what you were trying to do; the problem matters more than the proposed solution.

## Pull requests

Work in this repository goes through tasks and runs, the same way the agents work:

1. Open an issue first for anything bigger than a fix, so we agree on the task.
2. The task lives in `.apron/tasks.md`, with a `scope` (the files it may touch) and a `done-when`.
3. Start a run, report it, run the checks and close it: see [`docs/agent-protocol.md`](docs/agent-protocol.md). Your commit carries the trailer `Apron-Run: <run id>` and the run's timeline in its body.
4. Stay inside the scope. If the task needs more, say so in the pull request.

Agents and people follow the same rules; [`AGENTS.md`](AGENTS.md) is the short version.

## Code

- C++20 with the standard library. The one dependency is libghostty-vt, which draws the agents' terminals; the build fetches it and needs Zig 0.17 on the PATH. A new dependency needs a decision in the architecture log first. Code, comments and commit messages in English.
- Every string the interface shows goes through `tr()` or `trf()`, with its Portuguese entry in `src/core/i18n.cpp`; a test fails if one is missing.
- `cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build` must pass. Sanitizer builds (`-fsanitize=address,undefined`) should stay clean.
- Prefer deleting to adding: a new option, file or command needs a reason that a person will feel.

By contributing, you agree that your contribution is licensed under the Apache License 2.0, like the rest of the code.
