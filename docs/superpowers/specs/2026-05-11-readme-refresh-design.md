# README Refresh Design

## Goal

Replace the stale README with a high-quality GitHub-facing README that accurately presents Kode as an experimental JavaScript runtime built on V8 and libuv.

## Positioning

Kode should be presented as an ambitious but honest runtime project. It is not a Node clone. It explores smaller, explicit, structured host APIs while using V8 for JavaScript execution and libuv for event loop and I/O foundations.

## Structure

The README should include:

- Hero intro: what Kode is and why it exists.
- What works today: V8 execution, CommonJS local modules, `kode:fs`, `kode:path`, `Kode.scope`, `Kode.timeout`, `Kode.env`, and `Kode.args`.
- Quick start: build, run a file, run `-e`, run tests.
- API examples: short examples for modules, FS, path, scope, timeout, env, and args.
- Architecture: concise map of `src/core`, `src/v8`, `src/filesystem`, `src/concurrency`, and `src/http`.
- Design philosophy: explicit APIs, structured async, and no accidental Node compatibility.
- Status and limitations: experimental, not npm-compatible, no ESM/package resolution yet.
- Roadmap: networking/client APIs, crypto, file watchers, and stronger cancellation.
- Development workflow: verification command and contribution expectations.

## Style

- Confident but not misleading.
- Avoid claims of production readiness.
- Describe only implemented behavior as current behavior.
- Prefer examples and concrete commands over broad prose.
- Keep Markdown easy to skim.
