# Kode Sleep Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `Kode.sleep(ms, { signal? })` as a promise-first libuv-backed timer primitive.

**Architecture:** Store the runtime `uv_loop_t*` in the V8 embedder during initialization, install `Kode.sleep` from `kode_host.cc`, and back each sleep promise with a `uv_timer_t` plus persistent V8 handles. Keep `Kode.timeout` as cancellation-only and do not add global `setTimeout`.

**Tech Stack:** C++20, V8 embedder APIs, libuv timers, existing Kode structured error and promise helpers, Makefile smoke tests.

---

### Task 1: Loop Plumbing

**Files:**
- Modify: `src/v8/engine_iface.h`
- Modify: `src/v8/engine.cc`
- Modify: `src/core/runtime.cc`

- [ ] **Step 1: Add loop setter declaration**

In `src/v8/engine_iface.h`, include `uv.h` and declare:

```cpp
void setEventLoop(uv_loop_t* loop);
uv_loop_t* eventLoop();
```

- [ ] **Step 2: Store loop in V8 embedder**

In `src/v8/engine.cc`, add file-local state:

```cpp
static uv_loop_t* g_loop = nullptr;
```

Add implementations:

```cpp
void setEventLoop(uv_loop_t* loop) {
    g_loop = loop;
}

uv_loop_t* eventLoop() {
    return g_loop;
}
```

In `shutdown()`, set `g_loop = nullptr;` after clearing host state.

Also add matching no-V8 stub implementations under `#ifndef KODE_WITH_V8`:

```cpp
void setEventLoop(uv_loop_t*) {}
uv_loop_t* eventLoop() { return nullptr; }
```

- [ ] **Step 3: Pass runtime loop to V8**

In `src/core/runtime.cc`, before `kode::v8embed::initialize(&v8err)`, call:

```cpp
kode::v8embed::setEventLoop(loop);
```

- [ ] **Step 4: Verify no behavior change**

Run: `make build && make test-structured-runtime`

Expected: PASS.

- [ ] **Step 5: Commit loop plumbing**

```bash
git add src/v8/engine_iface.h src/v8/engine.cc src/core/runtime.cc
git commit -m "refactor: expose runtime loop to v8 host APIs"
```

### Task 2: Basic `Kode.sleep` Resolution

**Files:**
- Modify: `src/v8/kode_host.cc`
- Modify: `Makefile`
- Create: `tests/kode_sleep_basic.js`
- Create: `tests/kode_sleep_scope.js`

- [ ] **Step 1: Add failing smoke tests**

Create `tests/kode_sleep_basic.js`:

```js
Kode.scope(async () => {
  await Kode.sleep(0)
  console.log("sleep-basic", true)
})
```

Create `tests/kode_sleep_scope.js`:

```js
Kode.scope(async (scope) => {
  const first = scope.async(async () => {
    await Kode.sleep(0)
    return "alpha"
  })
  const second = scope.async(async () => "beta")
  console.log("sleep-scope", await first, await second)
})
```

Add checks to `Makefile` under `test-structured-runtime` near structured scope tests:

```make
	output="$$(./bin/kode tests/kode_sleep_basic.js)"; case "$$output" in *"sleep-basic true"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_sleep_scope.js)"; case "$$output" in *"[object Promise]"*) printf '%s\n' "$$output"; exit 1;; *"sleep-scope alpha beta"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
```

- [ ] **Step 2: Verify tests fail**

Run: `make build && make test-structured-runtime`

Expected: FAIL because `Kode.sleep` is not a function.

- [ ] **Step 3: Add sleep request struct and timer cleanup**

In `src/v8/kode_host.cc`, include `engine_iface.h`, `<uv.h>`, and add near the env state:

```cpp
struct SleepReq {
    uv_timer_t timer;
    v8::Isolate* isolate = nullptr;
    v8::Global<v8::Context> context;
    v8::Global<v8::Promise::Resolver> resolver;
    bool settled = false;
};

void CloseSleepReq(SleepReq* req) {
    uv_timer_stop(&req->timer);
    uv_close(reinterpret_cast<uv_handle_t*>(&req->timer), [](uv_handle_t* handle) {
        SleepReq* req = static_cast<SleepReq*>(handle->data);
        req->resolver.Reset();
        req->context.Reset();
        delete req;
    });
}
```

- [ ] **Step 4: Add timer callback**

Add:

```cpp
void SleepTimerCallback(uv_timer_t* timer) {
    SleepReq* req = static_cast<SleepReq*>(timer->data);
    v8::Isolate* isolate = req->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, req->context);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Promise::Resolver> resolver = v8::Local<v8::Promise::Resolver>::New(isolate, req->resolver);
    if (!req->settled) {
        req->settled = true;
        ResolvePromise(context, resolver, v8::Undefined(isolate));
        isolate->PerformMicrotaskCheckpoint();
    }
    CloseSleepReq(req);
}
```

- [ ] **Step 5: Add `Kode.sleep` callback**

Add:

```cpp
void SleepCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (args.Length() < 1 || !args[0]->IsNumber()) {
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EINVAL", "Kode.sleep requires a non-negative number", "Kode.sleep", ""));
        return;
    }
    double ms = args[0].As<v8::Number>()->Value();
    if (ms < 0) {
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EINVAL", "Kode.sleep requires a non-negative number", "Kode.sleep", ""));
        return;
    }
    uv_loop_t* loop = eventLoop();
    if (!loop) {
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EINTERNAL", "Runtime loop is not available", "Kode.sleep", ""));
        return;
    }
    v8::Local<v8::Promise::Resolver> resolver = NewResolver(isolate, context);
    auto* req = new SleepReq();
    req->isolate = isolate;
    req->context.Reset(isolate, context);
    req->resolver.Reset(isolate, resolver);
    uv_timer_init(loop, &req->timer);
    req->timer.data = req;
    uv_timer_start(&req->timer, SleepTimerCallback, static_cast<uint64_t>(ms), 0);
    args.GetReturnValue().Set(resolver->GetPromise());
}
```

- [ ] **Step 6: Install `Kode.sleep`**

In `InstallKodeHostApis`, before freezing `kode`, add:

```cpp
kode->Set(context, V8String(isolate, "sleep"), v8::Function::New(context, SleepCallback).ToLocalChecked()).FromMaybe(false);
```

- [ ] **Step 7: Verify Task 2 passes**

Run: `make build && make test-structured-runtime`

Expected: PASS.

- [ ] **Step 8: Commit basic sleep**

```bash
git add Makefile src/v8/kode_host.cc tests/kode_sleep_basic.js tests/kode_sleep_scope.js
git commit -m "feat: add Kode sleep timer"
```

### Task 3: Validation And Cancellation

**Files:**
- Modify: `src/v8/kode_host.cc`
- Modify: `Makefile`
- Create: `tests/kode_sleep_invalid.js`
- Create: `tests/kode_sleep_cancelled.js`
- Create: `tests/kode_timeout_not_callable.js`

- [ ] **Step 1: Add failing validation/cancellation tests**

Create `tests/kode_sleep_invalid.js`:

```js
try {
  Kode.sleep(-1)
} catch (err) {
  console.log("sleep-invalid", err.code, err.operation)
}
```

Create `tests/kode_sleep_cancelled.js`:

```js
Kode.scope(async () => {
  try {
    await Kode.sleep(10, { signal: Kode.timeout(0).signal })
  } catch (err) {
    console.log("sleep-cancelled", err.code, err.operation)
  }
})
```

Create `tests/kode_timeout_not_callable.js`:

```js
const timeout = Kode.timeout(1000)
Kode.scope(async () => {
  timeout(() => {})
})
```

Add checks to `Makefile`:

```make
	output="$$(./bin/kode tests/kode_sleep_invalid.js)"; case "$$output" in *"sleep-invalid EINVAL Kode.sleep"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_sleep_cancelled.js)"; case "$$output" in *"sleep-cancelled ECANCELED Kode.sleep"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_timeout_not_callable.js 2>&1; printf ' status:%s' "$$?")"; case "$$output" in *"status:0"*) printf '%s\n' "$$output"; exit 1;; *"timeout is not a function"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
```

- [ ] **Step 2: Verify tests fail**

Run: `make build && make test-structured-runtime`

Expected: FAIL at least for cancellation because Task 2 ignores `signal`.

- [ ] **Step 3: Add signal reading helper**

In `src/v8/kode_host.cc`, add:

```cpp
bool ReadSleepSignal(v8::Isolate* isolate,
                     v8::Local<v8::Context> context,
                     const v8::FunctionCallbackInfo<v8::Value>& args,
                     v8::Local<v8::Object>* signal_out) {
    if (args.Length() < 2 || args[1]->IsUndefined() || args[1]->IsNull()) return false;
    if (!args[1]->IsObject()) return false;
    v8::Local<v8::Object> options = args[1].As<v8::Object>();
    v8::Local<v8::Value> signal;
    if (!options->Get(context, V8String(isolate, "signal")).ToLocal(&signal)) return false;
    if (!signal->IsObject()) return false;
    *signal_out = signal.As<v8::Object>();
    return true;
}

bool SignalAborted(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> signal) {
    v8::Local<v8::Value> aborted;
    if (!signal->Get(context, V8String(isolate, "aborted")).ToLocal(&aborted)) return false;
    return aborted->BooleanValue(isolate);
}
```

- [ ] **Step 4: Reject immediately for aborted signal**

In `SleepCallback`, after validating `ms` and creating the resolver but before allocating `SleepReq`, add:

```cpp
v8::Local<v8::Object> signal;
if (ReadSleepSignal(isolate, context, args, &signal) && SignalAborted(isolate, context, signal)) {
    RejectPromise(context, resolver, CreateKodeError(isolate, context,
        "ECANCELED", "Sleep cancelled", "Kode.sleep", ""));
    args.GetReturnValue().Set(resolver->GetPromise());
    return;
}
```

This slice may skip async `signal.onabort` wiring if it would require a larger callback bridge; immediate cancellation is required by tests.

- [ ] **Step 5: Verify Task 3 passes**

Run: `make build && make test-structured-runtime`

Expected: PASS.

- [ ] **Step 6: Commit validation/cancellation**

```bash
git add Makefile src/v8/kode_host.cc tests/kode_sleep_invalid.js tests/kode_sleep_cancelled.js tests/kode_timeout_not_callable.js
git commit -m "feat: add sleep cancellation checks"
```

### Task 4: Docs And Full Verification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Document `Kode.sleep`**

Add `Kode.sleep(ms, { signal? })` to the `Kode` runtime API list.

Add an example near the structured scope or cancellation examples:

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

Clarify that `Kode.timeout(ms)` creates cancellation signals and `Kode.sleep(ms)` schedules delay.

- [ ] **Step 2: Run full verification**

Run: `make build && make test-v8-microtask && make test-structured-runtime && make test-http && make test-concurrency`

Expected: PASS.

- [ ] **Step 3: Commit docs**

```bash
git add -f README.md
git commit -m "docs: document Kode sleep"
```
