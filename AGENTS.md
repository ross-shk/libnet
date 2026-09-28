# libnet — Agent Guide

## Rules

- Don't invent workarounds, add compiler features you need to WISHLIST.md (make sure they are in like with TR25.084 or IBM Enterprise PL/I)
- Treat the compiler as buggy, log bugs into BUGS.md
- Main directive: strictly follow TR25.084-concrete-syntax.md and IBM Enterprise PL/I (see /Users/yarro/Development/PLI/references) when generating code, do not invent language and syntax features, and runtime functions.
- Make and refine a detailed plan for a task first, then implement
- PL/I card margins 2–72: nonblank `.pli`/`.inc` lines carry one leading space (text begins in column 2) and nothing past column 72, keep indentation consistent accross .pli and .inc files
- Add one-line comments stating the *intent* of the block that follows

## Token economy

- reason silently, return only one-line comments at crucial reasoning steps and the concise final answer
- `grep` for a symbol first, then `read` with `offset`/`limit`
- `edit` over `write`; batch related edits; parallel independent `bash` calls
- silo `tests/`: don't list, glob, or read the tree during source work — open only the specific test you are writing/fixing or running; `tests/*/out/` is gitignored scratch, never worth reading
