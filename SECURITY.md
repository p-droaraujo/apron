# Security

Apron runs agents' terminals and reaches other machines over SSH, so security reports matter.

## Reporting

Please do not open a public issue for a vulnerability. Use GitHub's private vulnerability reporting (the "Report a vulnerability" button in the Security tab), or write to mail@p-dro.com. You will get an answer within a week.

## What Apron trusts

- **The SSH login is the access.** Apron reaches another machine only through `ssh`, and can do there exactly what that login can. It adds no accounts, tokens or open ports.
- **The daemon socket is yours only.** `aprond` listens on a unix socket in a folder only your user can enter (mode 0700).
- **Files in `.apron/` are data, never commands.** Run ids and commits read from a repository are validated, and git runs without a shell. The commands in `.apron/checks.toml` do run, by design, when you call `apron check`: review them in a repository you did not write, as you would a Makefile.
