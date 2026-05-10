# Timers And Cancellation Design

## Goal

Kode should make deadlines and cancellation structural. The next phase adds a small timer/cancellation primitive that host APIs can understand without trying to clone Node's timer model.

The first user-facing API is:

```js
const fs = require("kode:fs")

await Kode.scope(async (scope) => {
  const timeout = Kode.timeout(100)

  try {
    await scope.async(() =>
      fs.read("README.md", { as: "text", signal: timeout.signal })
    )
  } catch (err) {
    console.log(err.code)
  }
})
```

## Principles

- Cancellation is explicit and inspectable.
- Deadlines are cancellation with a timer.
- Host APIs accept cancellation through options, not global state.
- Native callbacks must not resolve or reject a promise after the JS operation has already been cancelled.
- This phase does not forcibly stop libuv threadpool work.
- This phase does not add Node-compatible `setTimeout` as the main API.

## API Surface

### `Kode.timeout(ms)`

Shape:

```js
const timeout = Kode.timeout(50)
console.log(timeout.signal.aborted) // false

timeout.cancel()
console.log(timeout.signal.aborted) // true
console.log(timeout.signal.reason.code) // "ECANCELED"
```

Rules:

- `ms` must be a non-negative number.
- Returns `{ signal, cancel }`.
- `signal.aborted` starts as `false`.
- `signal.reason` starts as `undefined`.
- Calling `cancel()` aborts the signal immediately.
- When the deadline expires, the signal aborts automatically.
- Calling `cancel()` after the deadline is a no-op.
- Calling `cancel()` multiple times is a no-op after the first abort.

### Signal Object

Initial shape:

```js
{
  aborted: false,
  reason: undefined,
  onabort: undefined,
}
```

Rules:

- `aborted` is observable.
- `reason` is a structured `Error` when aborted.
- `onabort` is called once if it is a function at abort time.
- The cancellation error has:

```js
{
  code: "ECANCELED",
  operation: "Kode.timeout",
}
```

Future phases can add listener lists or `AbortSignal` compatibility. This phase keeps the object small.

## Host API Integration

The `kode:fs` APIs accept `signal` in their options:

```js
await fs.read("README.md", { as: "text", signal })
await fs.write("tmp/out.txt", "data", { create: "parents", signal })
```

Rules:

- If `signal.aborted` is already true, reject immediately with `signal.reason`.
- If `signal` aborts while native work is pending, reject the promise with `signal.reason`.
- If native work completes after cancellation, ignore the native completion.
- If native work completes first, clear the abort callback and settle normally.
- `fs.info(path)` may also accept `{ signal }` for consistency, but this phase only requires read/write cancellation tests.

## Native Implementation Direction

The current `Kode.scope` API is installed by JS bootstrap code in the V8 context. `Kode.timeout` should also be installed by bootstrap code for this phase.

Reasoning:

- The timer primitive is JS-visible state first.
- The current runtime already drains V8 microtasks and libuv work after script execution.
- A JS timer based on host-exposed primitive can come later if needed.

Implementation approach:

- Add `Kode.timeout(ms)` in the bootstrap script.
- Use a small host callback for actual timer scheduling if needed.
- For this phase, `cancel()` and already-aborted signals are enough to test FS cancellation deterministically.
- Add pending-promise guards in V8 FS bindings so cancelled promises are settled once.

## Error Model

Cancellation rejects with a JavaScript `Error`:

```js
try {
  const timeout = Kode.timeout(0)
  await fs.read("README.md", { as: "text", signal: timeout.signal })
} catch (err) {
  console.log(err.code)      // "ECANCELED"
  console.log(err.operation) // "Kode.timeout"
}
```

Rules:

- Cancellation errors use `ECANCELED`.
- Normal filesystem errors still use `ENOENT`, `EINVAL`, or `EIO`.
- Cancellation wins if it happens before native completion is delivered to JS.

## Testing Strategy

Add V8-level smoke tests through `./bin/kode` and `make test-structured-runtime`.

Required tests:

- `Kode.timeout(ms)` exposes `signal.aborted === false` initially.
- `timeout.cancel()` flips `signal.aborted` to true and sets `reason.code === "ECANCELED"`.
- `fs.read(path, { signal })` rejects immediately when passed an already-aborted signal.
- `fs.write(path, data, { signal })` rejects immediately when passed an already-aborted signal.
- Existing structured runtime and Kode-native FS tests keep passing.

Full verification:

```bash
make build && make test-v8-microtask && make test-structured-runtime && make test-http && make test-concurrency
```

## Scope Boundaries

In scope:

- `Kode.timeout(ms)`
- `timeout.cancel()`
- `signal.aborted`
- `signal.reason`
- Optional `signal.onabort`
- Immediate cancellation support in `fs.read` and `fs.write`
- Single-settlement guards for FS promises

Out of scope:

- Node-compatible `setTimeout`
- Full `AbortController` compatibility
- Event listener APIs
- Forcibly cancelling libuv threadpool work
- Cancellation support for networking
- Scheduler-backed task preemption
