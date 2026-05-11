# V8 Embedder Boundaries Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split `src/v8/engine.cc` into focused V8 embedder files without changing JavaScript behavior or public APIs.

**Architecture:** Keep the existing global-isolate/free-function model. Move helpers, built-ins, host APIs, and CommonJS loading into focused translation units while `engine.cc` remains responsible for V8 lifecycle, context setup, and script execution.

**Tech Stack:** C++20, V8 embedder API, libuv, Makefile smoke tests.

---

## File Structure

- Modify: `Makefile` to compile new V8 source files.
- Modify: `src/v8/engine.cc` to keep only lifecycle, console, initialization, shutdown, and `runScript`.
- Create: `src/v8/v8_helpers.h` and `src/v8/v8_helpers.cc` for shared V8/string/error/path helpers.
- Create: `src/v8/module_loader.h` and `src/v8/module_loader.cc` for CommonJS loading, require dispatch, module cache, entry directory, and cache cleanup.
- Create: `src/v8/builtins/fs.h` and `src/v8/builtins/fs.cc` for `CreateFsModule` and FS callbacks.
- Create: `src/v8/builtins/path.h` and `src/v8/builtins/path.cc` for `CreatePathModule` and path callbacks.
- Create: `src/v8/kode_host.h` and `src/v8/kode_host.cc` for Kode bootstrap, env snapshot, args install, and host cleanup.

## Task 1: Extract Shared V8 Helpers

**Files:**
- Create: `src/v8/v8_helpers.h`
- Create: `src/v8/v8_helpers.cc`
- Modify: `src/v8/engine.cc`
- Modify: `Makefile`

- [ ] **Step 1: Create helper declarations**

Create `src/v8/v8_helpers.h` with declarations for current shared helpers:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <v8.h>

namespace kode { namespace v8embed {

v8::Local<v8::String> V8String(v8::Isolate* isolate, const char* value);
v8::Local<v8::String> V8String(v8::Isolate* isolate, const std::string& value);

v8::Local<v8::Object> CreateKodeError(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      const std::string& code,
                                      const std::string& message,
                                      const std::string& operation,
                                      const std::string& path);

std::string NormalizePath(const std::filesystem::path& path);
std::string Dirname(const std::string& path);
std::string TrimTrailingSeparators(const std::string& path);
std::string LexicalPath(const std::filesystem::path& path);
std::string PathDirname(const std::string& path);
std::string PathBasename(const std::string& path);

bool ReadStringArg(v8::Isolate* isolate,
                   v8::Local<v8::Context> context,
                   const v8::FunctionCallbackInfo<v8::Value>& args,
                   int index,
                   const std::string& operation,
                   std::string* out);

v8::Local<v8::Promise::Resolver> NewResolver(v8::Isolate* isolate, v8::Local<v8::Context> context);
void ResolvePromise(v8::Local<v8::Context> context,
                    v8::Local<v8::Promise::Resolver> resolver,
                    v8::Local<v8::Value> value);
void RejectPromise(v8::Local<v8::Context> context,
                   v8::Local<v8::Promise::Resolver> resolver,
                   v8::Local<v8::Value> value);

std::string FormatTryCatch(v8::Isolate* isolate, v8::TryCatch& try_catch, const std::string& fallback_file);
void FreezeValue(v8::Local<v8::Context> context, v8::Local<v8::Value> value);

} } // namespace kode::v8embed
```

- [ ] **Step 2: Move helper definitions**

Create `src/v8/v8_helpers.cc` and move the corresponding definitions from `engine.cc` unchanged:

- `V8String`
- `NewResolver`
- `ResolvePromise`
- `RejectPromise`
- `CreateKodeError`
- `NormalizePath`
- `Dirname`
- `TrimTrailingSeparators`
- `LexicalPath`
- `PathDirname`
- `PathBasename`
- `ReadStringArg`
- `FormatTryCatch`
- `FreezeValue`

Include `v8_helpers.h` from `engine.cc` and remove the moved definitions from `engine.cc`.

- [ ] **Step 3: Update build and verify**

Add `src/v8/v8_helpers.cc` to the `APP` variable in `Makefile`.

Run: `make build && make test-structured-runtime`

Expected: build succeeds and all structured runtime smoke tests pass unchanged.

## Task 2: Extract `kode:path`

**Files:**
- Create: `src/v8/builtins/path.h`
- Create: `src/v8/builtins/path.cc`
- Modify: `src/v8/engine.cc`
- Modify: `Makefile`

- [ ] **Step 1: Create path built-in files**

Create `src/v8/builtins/path.h`:

```cpp
#pragma once

#include <v8.h>

namespace kode { namespace v8embed {

v8::Local<v8::Object> CreatePathModule(v8::Isolate* isolate, v8::Local<v8::Context> context);

} } // namespace kode::v8embed
```

Create `src/v8/builtins/path.cc` and move these definitions from `engine.cc` unchanged:

- `PathJoinCallback`
- `PathNormalizeCallback`
- `PathDirnameCallback`
- `PathBasenameCallback`
- `PathExtnameCallback`
- `PathIsAbsoluteCallback`
- `PathResolveCallback`
- `CreatePathModule`

Include `../v8_helpers.h` from `path.cc`.

- [ ] **Step 2: Wire path built-in**

Remove moved path definitions and forward declaration from `engine.cc`. `CreatePathModule` will later be called by `module_loader.cc`; until Task 5, if `RequireCallback` still lives in `engine.cc`, include `builtins/path.h` there.

Add `src/v8/builtins/path.cc` to `Makefile` `APP`.

Run: `make build && make test-structured-runtime`

Expected: existing `kode:path` tests pass unchanged.

## Task 3: Extract `kode:fs`

**Files:**
- Create: `src/v8/builtins/fs.h`
- Create: `src/v8/builtins/fs.cc`
- Modify: `src/v8/engine.cc`
- Modify: `Makefile`

- [ ] **Step 1: Create FS built-in files**

Create `src/v8/builtins/fs.h`:

```cpp
#pragma once

#include <v8.h>

namespace kode { namespace v8embed {

v8::Local<v8::Object> CreateFsModule(v8::Isolate* isolate, v8::Local<v8::Context> context);

} } // namespace kode::v8embed
```

Create `src/v8/builtins/fs.cc` and move the FS-only definitions from `engine.cc`:

- `AsyncReq`
- `PromiseReq`
- `FileKind`
- `CreateInfoValue`
- `GetStringOption`
- `GetSignalReasonIfAborted`
- `FSReadFileCallback`
- `FSReadTextCallback`
- `FSReadCallback`
- `FSWriteCallback`
- `FSInfoCallback`

Add `CreateFsModule` that returns the same object previously assembled inline in `RequireCallback`:

```cpp
v8::Local<v8::Object> CreateFsModule(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::Local<v8::Object> fs = v8::Object::New(isolate);
    fs->Set(context, V8String(isolate, "readFile"), v8::Function::New(context, FSReadFileCallback).ToLocalChecked()).FromMaybe(false);
    fs->Set(context, V8String(isolate, "readText"), v8::Function::New(context, FSReadTextCallback).ToLocalChecked()).FromMaybe(false);
    fs->Set(context, V8String(isolate, "read"), v8::Function::New(context, FSReadCallback).ToLocalChecked()).FromMaybe(false);
    fs->Set(context, V8String(isolate, "write"), v8::Function::New(context, FSWriteCallback).ToLocalChecked()).FromMaybe(false);
    fs->Set(context, V8String(isolate, "info"), v8::Function::New(context, FSInfoCallback).ToLocalChecked()).FromMaybe(false);
    return fs;
}
```

Include `../v8_helpers.h` and `../../filesystem/modern_fs.h` from `fs.cc`.

- [ ] **Step 2: Wire FS built-in**

If `RequireCallback` still lives in `engine.cc`, replace the inline FS object creation with:

```cpp
args.GetReturnValue().Set(CreateFsModule(isolate, context));
```

Add `src/v8/builtins/fs.cc` to `Makefile` `APP`.

Run: `make build && make test-structured-runtime`

Expected: all FS smoke tests pass unchanged.

## Task 4: Extract Kode Host APIs

**Files:**
- Create: `src/v8/kode_host.h`
- Create: `src/v8/kode_host.cc`
- Modify: `src/v8/engine.cc`
- Modify: `Makefile`

- [ ] **Step 1: Create host API files**

Create `src/v8/kode_host.h`:

```cpp
#pragma once

#include "engine_iface.h"
#include <string>
#include <v8.h>

namespace kode { namespace v8embed {

void CaptureEnvironment();
bool InstallKodeRuntimeBootstrap(v8::Isolate* isolate, v8::Local<v8::Context> context, std::string* error_out);
bool InstallKodeHostApis(v8::Isolate* isolate,
                         v8::Local<v8::Context> context,
                         const RuntimeOptions& runtime_options);
void ClearKodeHostState();

} } // namespace kode::v8embed
```

Create `src/v8/kode_host.cc` and move unchanged host/bootstrap definitions from `engine.cc`:

- `extern char** environ`
- `g_env_snapshot`
- `CaptureEnvironment`
- `EnvGetCallback`
- `EnvHasCallback`
- `EnvToObjectCallback`
- `InstallKodeHostApis`, changed to accept `const RuntimeOptions& runtime_options` instead of reading `g_runtime_options`
- `InstallKodeRuntimeBootstrap`

Add:

```cpp
void ClearKodeHostState() {
    g_env_snapshot.clear();
}
```

- [ ] **Step 2: Wire host APIs**

In `engine.cc`, include `kode_host.h`, remove moved definitions, and call:

```cpp
CaptureEnvironment();
InstallKodeRuntimeBootstrap(g_isolate, context, error_out);
InstallKodeHostApis(g_isolate, context, g_runtime_options);
```

In `shutdown`, replace `g_env_snapshot.clear()` with `ClearKodeHostState()`.

Add `src/v8/kode_host.cc` to `Makefile` `APP`.

Run: `make build && make test-structured-runtime`

Expected: Kode host API and timeout/scope tests pass unchanged.

## Task 5: Extract CommonJS Module Loader And Require Dispatch

**Files:**
- Create: `src/v8/module_loader.h`
- Create: `src/v8/module_loader.cc`
- Modify: `src/v8/engine.cc`
- Modify: `Makefile`

- [ ] **Step 1: Create module loader files**

Create `src/v8/module_loader.h`:

```cpp
#pragma once

#include <string>
#include <v8.h>

namespace kode { namespace v8embed {

void SetModuleEntryDirectory(const std::string& filename);
void ClearModuleCache();
void RequireCallback(const v8::FunctionCallbackInfo<v8::Value>& args);

} } // namespace kode::v8embed
```

Create `src/v8/module_loader.cc` and move module/require definitions from `engine.cc`:

- `g_module_cache`
- `g_module_dir_stack`
- `g_entry_dir`
- `IsLocalSpecifier`
- `ReadFileText`
- `FileExists`
- `ResolveLocalModulePath`
- `LoadLocalModule`
- `RequireCallback`

Add:

```cpp
void SetModuleEntryDirectory(const std::string& filename) {
    g_entry_dir = Dirname(NormalizePath(filename));
}

void ClearModuleCache() {
    for (auto& entry : g_module_cache) {
        entry.second.Reset();
    }
    g_module_cache.clear();
    g_module_dir_stack.clear();
    g_entry_dir.clear();
}
```

Include `v8_helpers.h`, `builtins/fs.h`, and `builtins/path.h` from `module_loader.cc`.

- [ ] **Step 2: Wire module loader**

In `engine.cc`, include `module_loader.h`, remove moved definitions, keep global `require` setup using `RequireCallback`, replace `g_entry_dir = Dirname(NormalizePath(filename));` with:

```cpp
SetModuleEntryDirectory(filename);
```

In `shutdown`, replace module cache cleanup with:

```cpp
ClearModuleCache();
```

Add `src/v8/module_loader.cc` to `Makefile` `APP`.

Run: `make build && make test-structured-runtime`

Expected: all CommonJS and built-in dispatch tests pass unchanged.

## Task 6: Final Integration And Verification

**Files:**
- Modify: all files touched above

- [ ] **Step 1: Inspect `engine.cc` scope**

Verify `src/v8/engine.cc` now contains only:

- non-V8 stubs
- V8 lifecycle globals
- `ConsoleLogCallback`
- `available`
- `setRuntimeOptions`
- `initialize`
- `shutdown`
- `runScript`

- [ ] **Step 2: Run full verification**

Run:

```sh
make build && make test-v8-microtask && make test-structured-runtime && make test-http && make test-concurrency
```

Expected: all commands exit 0.

- [ ] **Step 3: Inspect status**

Run: `git status --short`

Expected: only planned source files and the plan/spec docs are modified or untracked.

- [ ] **Step 4: Commit and push**

After review, stage only planned files and commit:

```sh
git commit -m "refactor: split v8 embedder boundaries"
git push
```

Expected: commit and push succeed.
