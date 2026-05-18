# Kode

Kode is an experimental JavaScript runtime built on V8 and libuv.

It is not trying to be a Node.js clone. Kode uses the same proven foundations for JavaScript execution and async I/O, but explores a smaller, more explicit runtime surface: Kode-native host modules, structured async ownership, clear cancellation, and predictable error objects.

The project is early, but it already runs real JavaScript through V8, supports local CommonJS modules, exposes Kode-native filesystem/path/crypto APIs, and has smoke tests for the runtime behavior that exists today.

## Why Kode Exists

Node made JavaScript useful outside the browser. Kode asks what a fresh runtime can look like if it keeps the good parts and rethinks the host API surface:

- explicit `kode:*` built-ins instead of accidental compatibility creep
- promise-first filesystem operations with structured results
- runtime-owned async scopes instead of loose background work
- cancellation signals with stable error shapes
- no `process` global by default
- V8 as the only JavaScript execution engine

Kode is a place to build those ideas incrementally and keep them testable.

## What Works Today

Current runtime capabilities:

- Execute JavaScript files and `-e` snippets through V8.
- Use `console.log`.
- Load local CommonJS modules with `require("./file")` and `require("./file.js")`.
- Use CommonJS `exports`, `module.exports`, `__filename`, and `__dirname`.
- Reuse modules through a normalized absolute-path module cache.
- Handle circular CommonJS dependencies, including partial exports.
- Use `require("kode:fs")` for Kode-native filesystem operations.
- Use `require("kode:path")` for pure path manipulation.
- Use `require("kode:crypto")` for small Kode-native crypto primitives.
- Use `Kode.scope(fn)` and `scope.async(fn)` for structured async ownership.
- Use `Kode.timeout(ms)` for cancellation signals.
- Read startup environment through read-only `Kode.env`.
- Read invocation data through read-only `Kode.args`.
- Verify core behavior with Makefile smoke tests.

## Quick Start

Build the runtime:

```sh
make build
```

Run a JavaScript file:

```sh
./bin/kode tests/modules/app_require_exports.js
```

Run inline code:

```sh
./bin/kode -e 'console.log("hello from Kode")'
```

Run the main verification suite:

```sh
make build && make test-v8-microtask && make test-structured-runtime && make test-http && make test-concurrency
```

## Examples

### Local CommonJS Modules

```js
// math.js
exports.add = (a, b) => a + b
```

```js
// app.js
const math = require("./math")
console.log("sum", math.add(2, 3))
```

```sh
./bin/kode app.js
```

### Kode-Native Filesystem

```js
const fs = require("kode:fs")

Kode.scope(async (scope) => {
  const file = await scope.async(() => fs.read("README.md", { as: "text" }))
  console.log(file.info.kind, file.info.mimeType)
  console.log(file.text.slice(0, 40))
})
```

Write a file and create parent directories intentionally:

```js
const fs = require("kode:fs")

Kode.scope(async (scope) => {
  const result = await scope.async(() =>
    fs.write("tmp/kode/out.txt", "hello", { create: "parents" })
  )

  console.log(result.bytesWritten, result.info.kind)
})
```

### Path Utilities

```js
const path = require("kode:path")

console.log(path.join("tmp", "kode", "out.txt"))
console.log(path.normalize("a/../b/./c.txt"))
console.log(path.dirname("src/v8/engine.cc"))
console.log(path.basename("src/v8/engine.cc"))
```

Kode intentionally exposes `kode:path`, not a bare Node-compatible `path` module.

### Crypto Hashing

```js
const crypto = require("kode:crypto")

const digest = crypto.hash("sha256", "hello")
console.log(digest.algorithm)
console.log(digest.hex)
```

Kode intentionally exposes `kode:crypto`, not a bare Node-compatible `crypto` module. The current crypto surface is deliberately small while Kode's byte and encoding APIs evolve.

### Structured Async Scope

```js
Kode.scope(async (scope) => {
  const first = scope.async(async () => "alpha")
  const second = scope.async(async () => "beta")

  console.log(await first, await second)
})
```

`Kode.activeOperations()` reports runtime-owned async work:

```js
console.log(Kode.activeOperations())
```

### Cancellation

```js
const fs = require("kode:fs")
const timeout = Kode.timeout(0)

Kode.scope(async (scope) => {
  try {
    await scope.async(() => fs.read("README.md", { as: "text", signal: timeout.signal }))
  } catch (err) {
    console.log(err.code, err.operation)
  }
})
```

Cancellation currently rejects work that sees an already-aborted signal. It does not forcibly stop libuv threadpool work that has already started.

### Environment And Arguments

Kode does not expose Node's `process` global. Runtime host data is available through Kode-native APIs:

```js
console.log(Kode.env.has("HOME"))
console.log(Kode.env.get("HOME"))
console.log(Kode.args.script)
console.log(Kode.args.values)
console.log(typeof process) // undefined
```

Run with script arguments:

```sh
./bin/kode app.js alpha beta
```

## Runtime APIs

### `require("kode:fs")`

Current Kode-native filesystem surface:

- `fs.read(path, { as: "text", signal? }) -> Promise<{ text, info }>`
- `fs.write(path, data, { create?: "none" | "parents", signal? }) -> Promise<{ bytesWritten, info }>`
- `fs.info(path) -> Promise<info | null>`
- `fs.readText(path) -> Promise<string>` compatibility helper
- `fs.readFile(path, callback)` legacy callback helper

Errors are JavaScript `Error` objects with stable fields such as `code`, `operation`, and `path`.

### `require("kode:path")`

Current path surface:

- `join(...parts)`
- `normalize(path)`
- `dirname(path)`
- `basename(path)`
- `extname(path)`
- `isAbsolute(path)`
- `resolve(...parts)`

These are string/path transformations only. They do not check whether files exist.

### `require("kode:crypto")`

Current crypto surface:

- `hash("sha256", data) -> { algorithm, hex }`

`data` is currently a JavaScript string. Kode hashes the UTF-8 bytes and returns a lowercase hexadecimal SHA-256 digest. Unsupported algorithms throw structured errors with `code: "EUNSUPPORTED_ALGORITHM"` and `operation: "kode:crypto.hash"`.

### `Kode`

Current runtime surface:

- `Kode.scope(fn)`
- `scope.async(fn)` inside a scope
- `Kode.activeOperations()`
- `Kode.timeout(ms)`
- `Kode.env.get(name)`
- `Kode.env.has(name)`
- `Kode.env.toObject()`
- `Kode.args.executable`
- `Kode.args.script`
- `Kode.args.values`

`Kode`, `Kode.env`, `Kode.args`, and `Kode.args.values` are protected against accidental replacement from JavaScript.

## Architecture

High-level structure:

```text
src/
  main.cc                 CLI entry point and argument handling
  core/                   Runtime lifecycle, event loop, execution wiring
  v8/                     V8 embedder and JavaScript host bindings
    engine.cc             V8 lifecycle, context setup, runScript
    module_loader.*       CommonJS require, local module cache, built-in dispatch
    kode_host.*           Kode bootstrap, env, args
    v8_helpers.*          Shared V8 helpers and error formatting
    builtins/fs.*         kode:fs bindings
    builtins/path.*       kode:path bindings
    builtins/crypto.*     kode:crypto bindings
  filesystem/             ModernFS and legacy filesystem internals
  concurrency/            Cooperative task/concurrency experiments
  http/                   Native HTTP server experiments
  parser/                 Legacy learning scaffold, not the runtime execution path
tests/                    JavaScript and C++ smoke tests
```

Execution flow:

1. `main.cc` parses CLI input and captures invocation data.
2. `KodeRuntime` initializes libuv, filesystem internals, concurrency internals, and V8.
3. The V8 embedder creates the isolate/context and installs `require`, `console`, and `Kode`.
4. Scripts execute through V8 only.
5. The libuv loop drains pending host work.
6. Runtime shutdown clears module and host state, then disposes V8.

## Design Philosophy

Kode favors explicit runtime design over compatibility by accident.

- Use `kode:*` for Kode-native built-ins.
- Keep APIs small until behavior is proven by tests.
- Prefer structured results over overloaded call signatures.
- Prefer stable error fields over string matching.
- Keep async work owned by runtime scopes where possible.
- Do not add Node compatibility unless there is a concrete reason.

Compatibility aliases may exist while the project evolves, but new APIs should be Kode-native first.

## Status And Limitations

Kode is experimental. It is useful for runtime development and API exploration, not production workloads.

Current limitations:

- No npm package resolution.
- No `package.json` module resolution.
- No ESM loader.
- No Node `process` global.
- No general Node standard-library compatibility target.
- Cancellation is cooperative and currently strongest before host work starts.
- Parser code remains in the repository as a learning scaffold, but normal JS execution uses V8.

## Roadmap

Near-term runtime work:

- Byte and encoding primitives to support future binary APIs.
- More `kode:crypto` primitives once byte handling is clearer.
- HTTP client APIs.
- File watchers.
- More robust cancellation for active host operations.
- Stronger active-operation diagnostics.
- More module loader behavior only when needed.

Longer-term possibilities:

- Dedicated package/module story that is not just Node compatibility by default.
- Debugging/profiling hooks.
- Cleaner concurrency integration with JS promises and host tasks.
- More complete networking primitives.

## Development

Build:

```sh
make build
```

Run focused runtime smoke tests:

```sh
make test-structured-runtime
```

Run full verification before committing runtime changes:

```sh
make build && make test-v8-microtask && make test-structured-runtime && make test-http && make test-concurrency
```

Clean generated binaries:

```sh
make clean
```

## Repository Notes

- `v8/` and `libuv/` are expected to contain local build artifacts and headers used by the Makefile.
- `bin/` is generated.
- `.worktrees/` is ignored and used for isolated feature worktrees.
- Markdown docs are force-added when needed because this repository currently ignores `*.md`.

## License

No license file is currently present. Add one before distributing or accepting external contributions.
