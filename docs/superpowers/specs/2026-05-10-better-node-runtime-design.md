# Better Node Runtime Design

## Goal

Kode should become a JavaScript runtime that keeps Node's systems-level usefulness while avoiding Node's loose async ownership model. The runtime should expose Zig-inspired structured concurrency as a JavaScript API on top of V8.

The first differentiator is explicit async ownership:

```js
await Kode.scope(async (scope) => {
  const a = scope.async(() => readText("a.txt"))
  const b = scope.async(() => readText("b.txt"))

  console.log(await a, await b)
})
```

In this model, async work is not an anonymous promise or hidden event-loop handle. It belongs to a runtime scope, can be joined, can be cancelled, and can be explained by diagnostics.

## Current Context

The repository is already moving toward this direction:

- V8 is the only execution path that can become a real JavaScript runtime.
- libuv is the right backend for the event loop, timers, file I/O, and networking.
- `KodeRuntime` is the right place to wire host APIs into V8.
- The parser path is useful as a learning/demo path, but should not drive future runtime behavior.
- The existing concurrency runtime has tasks, scheduling, yielding, and channels, but JS integration should move toward V8-hosted APIs.

## Runtime Lifecycle

The runtime lifecycle should be explicit:

```text
initialize -> create root runtime scope -> execute module/script -> drain owned async work -> shutdown
```

`KodeRuntime` remains the top-level orchestrator. It owns V8 initialization, host API registration, libuv loop integration, root scope creation, execution, draining, and shutdown.

Shutdown should be explainable. If the process remains alive, Kode should be able to report which scopes, tasks, timers, filesystem operations, or network handles are still active.

## Structured Concurrency API

The core JS API is `Kode.scope(fn)`.

```js
await Kode.scope(async (scope) => {
  const result = scope.async(() => work())
  console.log(await result)
})
```

Rules:

- `Kode.scope(fn)` creates an owned async region.
- `scope.async(fn)` starts work owned by that scope.
- `await task` joins a scoped task and returns its result.
- A scope does not finish until all owned tasks finish or are cancelled.
- If one scoped task fails, the scope cancels remaining sibling tasks.
- Detached work is not the default.
- If detached work is added, it must be explicit, for example `Kode.detach(fn)`.

This is inspired by Zig's explicit async/await model: work has an owner, waiting is explicit, and the runtime can reason about live async frames.

## Cancellation And Timeout

Cancellation should be structural, not bolted on per API.

Initial shape:

```js
const timeout = Kode.timeout(500)

await readText("large.txt", {
  signal: timeout.signal,
})
```

Rules:

- All async host APIs accept an optional `signal`.
- Timeout is cancellation with a deadline.
- Cancelling a scope cancels its owned tasks.
- Runtime shutdown cancels unfinished scoped work in a predictable order.
- Native callbacks must not run into a destroyed scope or isolate.

## Host API Direction

Kode's standard library should be smaller than Node's, but more consistent.

Preferred module style:

```js
import { readText, writeText, exists } from "kode:fs"
import { serve } from "kode:http"
import { env, args, cwd } from "kode:process"
import { timeout, scope } from "kode:runtime"
```

Principles:

- Prefer `kode:*` built-ins over Node-compatible globals.
- Make APIs promise-first.
- Support explicit cancellation on async operations.
- Use stable error codes and predictable error shapes.
- Keep sync APIs minimal and deliberate.
- Avoid recreating Node's overloaded API surface.

Filesystem should be the first concrete host API because it exercises V8 bindings, libuv work, callbacks, error conversion, cancellation, and shutdown behavior.

Initial filesystem API:

```js
const text = await readText("input.txt")
await writeText("out/result.txt", text)
const ok = await exists("out/result.txt")
```

## Error Model

Errors should be stable and inspectable.

Example:

```js
try {
  await readText("missing.txt")
} catch (err) {
  console.log(err.code)      // "ENOENT"
  console.log(err.message)   // readable explanation
  console.log(err.operation) // "fs.readText"
  console.log(err.path)      // "missing.txt"
}
```

Rules:

- Host errors include a stable `code`.
- Host errors include an `operation`.
- Path-based operations include the relevant `path`.
- Cancellation uses a distinct stable error code.
- C++ errors are converted consistently before entering JS.

## Diagnostics

Runtime diagnostics are part of the product, not an afterthought.

The runtime should eventually be able to answer:

- Which scopes are active?
- Which tasks are running or waiting?
- Which task owns a filesystem, timer, or network operation?
- Why has shutdown not completed?
- Which task failed first and caused sibling cancellation?

The first implementation only needs internal tracking and a simple diagnostic string or debug print. A polished JS API can come later.

## Testing Strategy

The repo should stay buildable while this foundation is added.

Required verification path:

- Keep `make build` working.
- Keep existing HTTP and concurrency smoke tests working.
- Add one V8-level smoke test per new host API.
- Test successful scoped task completion.
- Test sibling cancellation when one scoped task fails.
- Test filesystem success and missing-file errors through V8.
- Test that the runtime exits when no scoped work remains.
- Test diagnostics for a runtime that still has active work.

## First Implementation Slice

The first slice should be deliberately narrow:

1. Add runtime operation tracking to `KodeRuntime`.
2. Add a minimal internal representation for runtime scopes and scoped tasks.
3. Expose `Kode.scope(fn)` and `scope.async(fn)` in V8.
4. Make `await task` join the owned task.
5. Implement sibling cancellation for a failed scoped task.
6. Define the `kode:fs` API contract.
7. Bind `readText` as the first real filesystem function.
8. Add V8 smoke tests for success, missing-file errors, and scope failure behavior.

This keeps the work focused on Kode's identity: a JavaScript runtime with explicit async ownership, predictable shutdown, and host APIs designed around structured concurrency.
