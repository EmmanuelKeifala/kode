# V8 Embedder Boundaries Refactor Design

## Goal

Split `src/v8/engine.cc` into focused implementation units without changing JavaScript behavior or public APIs.

## Scope

This is a behavior-preserving refactor only. It does not add `kode:crypto` or any new runtime feature. Existing smoke tests must pass unchanged.

## Architecture

The current `engine.cc` owns too many responsibilities: V8 lifecycle, CommonJS loading, built-in module dispatch, FS bindings, path bindings, Kode host APIs, bootstrap JavaScript, error formatting, and shared helpers. The refactor should keep the current global-isolate model but move each subsystem into a focused file.

New file boundaries:

- `src/v8/engine.cc`
  - V8 platform, isolate, allocator, and context lifecycle.
  - `available`, `initialize`, `shutdown`, `setRuntimeOptions`, and `runScript`.
  - Global context setup by delegating to helper modules.
- `src/v8/v8_helpers.h/.cc`
  - `V8String`.
  - `CreateKodeError`.
  - `FormatTryCatch`.
  - shared promise helpers if needed outside one subsystem.
  - path string helpers shared by module loading and `kode:path`.
- `src/v8/module_loader.h/.cc`
  - Local CommonJS resolution, loading, wrapping, and cache.
  - Module directory stack and entry directory.
  - `RequireCallback` or a factory that installs the global `require` function.
  - Built-in dispatch to `kode:fs` and `kode:path`.
- `src/v8/builtins/fs.h/.cc`
  - `CreateFsModule`.
  - All FS callback implementations and FS async request structs.
- `src/v8/builtins/path.h/.cc`
  - `CreatePathModule`.
  - All `kode:path` callbacks and path-specific helpers.
- `src/v8/kode_host.h/.cc`
  - `InstallKodeRuntimeBootstrap`.
  - `InstallKodeHostApis`.
  - Environment snapshot and `Kode.args` installation.

## Data Flow And Lifetimes

The refactor should preserve the current global-isolate model rather than introducing classes.

State ownership:

- `engine.cc` owns `g_platform`, `g_isolate`, `g_allocator`, `g_context`, and `g_runtime_options`.
- `module_loader.cc` owns module cache, module directory stack, and entry directory.
- `kode_host.cc` owns the environment snapshot.
- FS request structs live in `builtins/fs.cc` because only FS callbacks use them.

Initialization flow:

1. `engine.cc` creates the isolate and context.
2. `engine.cc` installs global `require` by delegating to `module_loader`.
3. `engine.cc` installs `console.log`.
4. `engine.cc` calls `CaptureEnvironment` from `kode_host`.
5. `engine.cc` calls `InstallKodeRuntimeBootstrap` from `kode_host`.
6. `engine.cc` calls `InstallKodeHostApis` from `kode_host`.
7. `engine.cc` stores the persistent context.

Runtime flow:

- `runScript` sets the module loader entry directory before executing a script.
- Local `require` delegates to `LoadLocalModule`.
- `require("kode:fs")` delegates to `CreateFsModule`.
- `require("kode:path")` delegates to `CreatePathModule`.

Shutdown flow:

- `engine.cc` calls `ClearModuleCache()` from `module_loader`.
- `engine.cc` calls `ClearKodeHostState()` from `kode_host`.
- `engine.cc` resets the V8 context, disposes the isolate, deletes the allocator, and disposes V8/platform as it does today.

## Migration Sequence

1. Extract shared helpers into `v8_helpers`.
2. Extract `kode:path` into `builtins/path`.
3. Extract `kode:fs` into `builtins/fs`.
4. Extract Kode bootstrap/env/args into `kode_host`.
5. Extract CommonJS loader and require dispatch into `module_loader`.
6. Reduce `engine.cc` to lifecycle, global setup, and `runScript`.
7. Update `Makefile` `APP` sources.
8. Run full verification.

## Testing

No new JavaScript behavior is expected. Existing tests should pass unchanged.

Full verification command:

```sh
make build && make test-v8-microtask && make test-structured-runtime && make test-http && make test-concurrency
```

Request code review after the refactor with focus on:

- V8 handle lifetimes.
- Persistent handle cleanup.
- Module cache behavior.
- Built-in module dispatch regressions.
- Host API immutability regressions.
- Any behavior drift from existing tests.

## Constraints

- Do not change JavaScript APIs.
- Do not add new runtime features.
- Do not rename `Kode`, `kode:fs`, `kode:path`, or CommonJS globals.
- Keep globals file-local where possible.
- Prefer free functions over classes for this refactor.
- Keep changes mechanical and reviewable.
