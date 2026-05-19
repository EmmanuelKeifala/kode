# Kode Sleep Design

## Goal

Add a small promise-first timer primitive so Kode scripts can schedule delayed async work without overloading `Kode.timeout` or adding Node/browser-compatible `setTimeout` yet.

## Scope

This milestone adds:

- `Kode.sleep(ms) -> Promise<void>`.
- `Kode.sleep(ms, { signal }) -> Promise<void>`.

It does not add:

- global `setTimeout` or `clearTimeout`.
- callback-style timer APIs.
- interval/repeating timers.
- a callable `Kode.timeout` object.

`Kode.timeout(ms)` remains a cancellation-signal factory. `Kode.sleep(ms)` is the scheduling primitive.

## API

```js
await Kode.sleep(1000)
```

With structured scope:

```js
Kode.scope(async (scope) => {
  const first = scope.async(async () => {
    await Kode.sleep(1000)
    return "alpha"
  })

  const second = scope.async(async () => "beta")

  console.log(await first, await second)
})
```

With cancellation:

```js
const timeout = Kode.timeout(0)

try {
  await Kode.sleep(1000, { signal: timeout.signal })
} catch (err) {
  console.log(err.code, err.operation)
}
```

Expected cancellation output:

```text
ECANCELED Kode.sleep
```

## Behavior

`Kode.sleep(ms)`:

- accepts a non-negative JavaScript number.
- returns a Promise.
- resolves with `undefined` after at least `ms` milliseconds.
- rejects with `EINVAL` and operation `Kode.sleep` for missing, non-number, or negative `ms`.

`Kode.sleep(ms, { signal })`:

- rejects immediately with `ECANCELED` and operation `Kode.sleep` when `signal.aborted` is already true.
- should arrange for cancellation through `signal.onabort` when feasible in this slice.
- does not need to support multiple abort listeners yet because current `Kode.timeout` only exposes a single `onabort` slot.

If the timer fires first, the Promise resolves once. If the signal aborts first, the Promise rejects once. Later timer or abort events should not settle the Promise again.

## Architecture

Implement `Kode.sleep` as a V8 host API backed by libuv timers.

Recommended file ownership:

- `src/v8/kode_host.cc`
  - installs `Kode.sleep` with other `Kode` host APIs.
  - owns the sleep callback and small request struct unless it grows large enough to split later.
- `src/v8/kode_host.h`
  - no public API change expected.
- `src/v8/engine.cc`
  - exposes the active libuv loop to host bindings if needed.
- `src/v8/engine_iface.h`
  - declares a minimal loop accessor if needed by `kode_host.cc`.
- `src/core/runtime.cc`
  - passes or exposes the runtime loop to V8 if the current global loop is not otherwise reachable.

Keep the implementation minimal. Reuse existing `CreateKodeError`, `NewResolver`, `ResolvePromise`, `RejectPromise`, and microtask checkpoint helpers where possible.

## Event Loop And Lifetime

Each sleep request owns:

- a `uv_timer_t`.
- a persistent Promise resolver.
- a persistent context.
- a settled flag.

On timer fire:

1. Resolve the Promise with `undefined` if not settled.
2. Mark settled.
3. Stop and close the timer.
4. Reset persistent V8 handles after close.
5. Run a microtask checkpoint.

On immediate cancellation:

1. Reject the Promise before starting the timer.
2. Return the rejected Promise.

On async cancellation through `signal.onabort`:

1. Reject if not settled.
2. Stop and close the timer.
3. Reset persistent V8 handles after close.
4. Run a microtask checkpoint.

## Testing

Add smoke tests for:

- `await Kode.sleep(0)` resolves and allows code after it to run.
- `Kode.sleep(-1)` throws or rejects with `EINVAL Kode.sleep`.
- `Kode.sleep(10, { signal: Kode.timeout(0).signal })` rejects with `ECANCELED Kode.sleep`.
- `Kode.scope` can await delayed work without printing `[object Promise]`.
- The original misuse is clearer: calling the object returned by `Kode.timeout(1000)` throws `TypeError: timeout is not a function` through the top-level promise rejection path.

Full verification command:

```sh
make build && make test-v8-microtask && make test-structured-runtime && make test-http && make test-concurrency
```

## Constraints

- Do not add global `setTimeout`.
- Do not make `Kode.timeout` callable.
- Keep `Kode.timeout` focused on cancellation signals.
- Keep `Kode.sleep` promise-first and Kode-native.
- Do not add interval support in this slice.
