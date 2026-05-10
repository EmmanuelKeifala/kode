# Runtime Polish And Host APIs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve local CommonJS loading, add Kode-native host APIs, and remove parser fallback so V8 is the only normal JavaScript execution path.

**Architecture:** Keep this milestone inside the current V8/runtime embedding layer. Extend `src/v8/engine.cc` for module polish and host APIs, add small runtime options plumbing through `KodeRuntime`, and make `ExecuteString` fail on V8 errors instead of parsing fallback source.

**Tech Stack:** C++20, V8 embedder API, `std::filesystem`, environment variables from `environ`, Makefile smoke tests.

---

## File Structure

- Modify: `src/v8/engine_iface.h` to add runtime options and preserve filename-aware `runScript`.
- Modify: `src/v8/engine.cc` to add extensionless module resolution, filename-aware error text, `kode:path`, `Kode.env`, and `Kode.args` bootstrap data.
- Modify: `src/core/runtime.h` to store executable/script/user args and expose setters.
- Modify: `src/core/runtime.cc` to pass runtime options to V8 initialization and remove parser fallback from `ExecuteString`.
- Modify: `src/main.cc` to preserve script arguments and `-e` arguments in `Kode.args`.
- Modify: `Makefile` to add smoke checks.
- Create: `tests/modules/app_require_extensionless.js`.
- Create: `tests/modules/lib/uses_parent_extensionless.js`.
- Create: `tests/modules/app_nested_extensionless.js`.
- Create: `tests/modules/cycle_a.js`.
- Create: `tests/modules/cycle_b.js`.
- Create: `tests/modules/app_cycle.js`.
- Create: `tests/modules/bad_syntax.js`.
- Create: `tests/modules/app_module_syntax_error.js`.
- Create: `tests/modules/runtime_error.js`.
- Create: `tests/modules/app_module_runtime_error.js`.
- Create: `tests/kode_path_basic.js`.
- Create: `tests/kode_path_no_bare_alias.js`.
- Create: `tests/kode_env_basic.js`.
- Create: `tests/kode_args_basic.js`.
- Create: `tests/kode_no_process_global.js`.
- Create: `tests/parser_fallback_syntax_error.js`.
- Create: `tests/parser_fallback_runtime_error.js`.

## Task 1: Require Polish

**Files:**
- Modify: `src/v8/engine.cc`
- Modify: `Makefile`
- Create module tests listed above under `tests/modules/`

- [ ] **Step 1: Write failing extensionless require tests**

Create `tests/modules/app_require_extensionless.js`:

```js
const math = require("./math")
console.log("module-extensionless", math.add(6, 7))
```

Create `tests/modules/lib/uses_parent_extensionless.js`:

```js
const math = require("../math")
exports.sum = math.add(8, 9)
```

Create `tests/modules/app_nested_extensionless.js`:

```js
const nested = require("./lib/uses_parent_extensionless")
console.log("module-nested-extensionless", nested.sum)
```

Append checks to `test-structured-runtime` after existing module checks:

```make
	output="$$(./bin/kode tests/modules/app_require_extensionless.js)"; case "$$output" in *"module-extensionless 13"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_nested_extensionless.js)"; case "$$output" in *"module-nested-extensionless 17"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
```

Run: `make test-structured-runtime`

Expected: FAIL because extensionless local modules are not resolved.

- [ ] **Step 2: Implement extensionless local resolution**

In `src/v8/engine.cc`, replace direct filename construction inside `LoadLocalModule` with a helper that resolves the final path before cache lookup:

```cpp
bool FileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::string ResolveLocalModulePath(const std::string& base_dir, const std::string& specifier) {
    std::string literal = NormalizePath(std::filesystem::path(base_dir) / specifier);
    if (FileExists(literal)) return literal;
    if (std::filesystem::path(literal).extension().empty()) {
        std::string with_js = literal + ".js";
        if (FileExists(with_js)) return with_js;
        return with_js;
    }
    return literal;
}
```

Then in `LoadLocalModule` use:

```cpp
const std::string filename = ResolveLocalModulePath(base_dir, specifier);
```

Run: `make test-structured-runtime`

Expected: PASS for extensionless require tests.

- [ ] **Step 3: Write circular dependency tests**

Create `tests/modules/cycle_a.js`:

```js
exports.name = "a-start"
const b = require("./cycle_b")
exports.bSaw = b.sawA
exports.name = "a-done"
```

Create `tests/modules/cycle_b.js`:

```js
const a = require("./cycle_a")
exports.sawA = a.name
```

Create `tests/modules/app_cycle.js`:

```js
const a = require("./cycle_a")
const b = require("./cycle_b")
console.log("module-cycle", a.name, a.bSaw, b.sawA)
```

Append check:

```make
	output="$$(./bin/kode tests/modules/app_cycle.js)"; case "$$output" in *"module-cycle a-done a-start a-start"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
```

Run: `make test-structured-runtime`

Expected: PASS because the current early cache behavior already supports this cycle.

- [ ] **Step 4: Write filename-aware module error tests**

Create `tests/modules/bad_syntax.js`:

```js
exports.value =
```

Create `tests/modules/app_module_syntax_error.js`:

```js
require("./bad_syntax")
```

Create `tests/modules/runtime_error.js`:

```js
throw new Error("module exploded")
```

Create `tests/modules/app_module_runtime_error.js`:

```js
require("./runtime_error")
```

Append checks:

```make
	output="$$(./bin/kode tests/modules/app_module_syntax_error.js 2>&1 || true)"; case "$$output" in *"bad_syntax.js"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_module_runtime_error.js 2>&1 || true)"; case "$$output" in *"runtime_error.js"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
```

Run: `make test-structured-runtime`

Expected: FAIL until module errors include filenames.

- [ ] **Step 5: Implement filename-aware module errors**

Add a helper in `src/v8/engine.cc`:

```cpp
std::string FormatTryCatch(v8::Isolate* isolate, v8::TryCatch& try_catch, const std::string& fallback_file) {
    std::string file = fallback_file;
    int line = 0;
    v8::Local<v8::Message> message = try_catch.Message();
    if (!message.IsEmpty()) {
        v8::String::Utf8Value resource(isolate, message->GetScriptResourceName());
        if (resource.length() > 0) file = std::string(*resource, resource.length());
        line = message->GetLineNumber(isolate->GetCurrentContext()).FromMaybe(0);
    }
    v8::String::Utf8Value err(isolate, try_catch.Exception());
    std::string text = err.length() ? std::string(*err, err.length()) : "JavaScript error";
    if (line > 0) return file + ":" + std::to_string(line) + ": " + text;
    return file + ": " + text;
}
```

Compile top-level and module wrapper scripts with `v8::ScriptOrigin` using their filenames, and use `FormatTryCatch` in both `runScript` and `LoadLocalModule` error paths.

Run: `make test-structured-runtime`

Expected: PASS for module filename error checks.

## Task 2: Kode Host APIs

**Files:**
- Modify: `src/v8/engine_iface.h`
- Modify: `src/v8/engine.cc`
- Modify: `src/core/runtime.h`
- Modify: `src/core/runtime.cc`
- Modify: `src/main.cc`
- Modify: `Makefile`
- Create host API tests listed above

- [ ] **Step 1: Write failing host API tests**

Create `tests/kode_path_basic.js`:

```js
const path = require("kode:path")
console.log("path-join", path.join("a", "b", "c.txt"))
console.log("path-normalize", path.normalize("a/../b/./c.txt"))
console.log("path-dir-base", path.dirname("a/b/c.txt"), path.basename("a/b/c.txt"))
console.log("path-ext-abs", path.extname("a/b/c.txt"), path.isAbsolute("/tmp/file.txt"))
```

Create `tests/kode_path_no_bare_alias.js`:

```js
try {
  require("path")
} catch (err) {
  console.log("path-no-bare", err.code, err.operation)
}
```

Create `tests/kode_env_basic.js`:

```js
const env = Kode.env.toObject()
console.log("env-has", Kode.env.has("KODE_ENV_TEST"))
console.log("env-get", Kode.env.get("KODE_ENV_TEST"))
console.log("env-missing", Kode.env.get("KODE_ENV_DOES_NOT_EXIST") === undefined)
console.log("env-object", env.KODE_ENV_TEST)
```

Create `tests/kode_args_basic.js`:

```js
console.log("args-script", Kode.args.script.endsWith("tests/kode_args_basic.js"))
console.log("args-values", Kode.args.values.join(","))
```

Create `tests/kode_no_process_global.js`:

```js
console.log("no-process", typeof process)
```

Append checks:

```make
	output="$$(./bin/kode tests/kode_path_basic.js)"; case "$$output" in *"path-join a/b/c.txt"*"path-normalize b/c.txt"*"path-dir-base a/b c.txt"*"path-ext-abs .txt true"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_path_no_bare_alias.js)"; case "$$output" in *"path-no-bare EUNSUPPORTED_MODULE module.require"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(KODE_ENV_TEST=hello ./bin/kode tests/kode_env_basic.js)"; case "$$output" in *"env-has true"*"env-get hello"*"env-missing true"*"env-object hello"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_args_basic.js alpha beta)"; case "$$output" in *"args-script true"*"args-values alpha,beta"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_no_process_global.js)"; case "$$output" in *"no-process undefined"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
```

Run: `make test-structured-runtime`

Expected: FAIL because `kode:path`, `Kode.env`, and `Kode.args` do not exist.

- [ ] **Step 2: Add runtime options plumbing**

In `src/v8/engine_iface.h`, add:

```cpp
#include <vector>

struct RuntimeOptions {
    std::string executable;
    std::string script;
    std::vector<std::string> args;
};

void setRuntimeOptions(const RuntimeOptions& options);
```

In `src/v8/engine.cc`, store `RuntimeOptions g_runtime_options;` and define `setRuntimeOptions` in both V8 and non-V8 branches.

In `src/core/runtime.h`, add private fields and a public setter:

```cpp
std::string executable_;
std::string script_;
std::vector<std::string> args_;

void SetInvocation(const std::string& executable, const std::string& script, const std::vector<std::string>& args);
```

In `src/core/runtime.cc`, implement `SetInvocation` and call `kode::v8embed::setRuntimeOptions(...)` before V8 initialization in `Initialize()`.

- [ ] **Step 3: Preserve script and eval args**

In `src/main.cc`, before `runtime.Initialize()`, parse invocation into `script`, `code`, and `user_args`. For file execution, stop option parsing after the first script path and preserve remaining args. For `-e`, preserve args after the code string.

Use:

```cpp
runtime.SetInvocation(argv[0], script, user_args);
```

Then call `runtime.Initialize()` and execute either the script or code once.

Run: `make test-structured-runtime`

Expected: host API tests still fail until bootstrap/API implementation is added, but existing script execution should still work.

- [ ] **Step 4: Implement `kode:path`**

Add `CreatePathModule(isolate, context)` in `src/v8/engine.cc` with function callbacks for `join`, `normalize`, `dirname`, `basename`, `extname`, `isAbsolute`, and `resolve`. Each callback should validate string arguments and return slash-normalized strings.

Update `RequireCallback`:

```cpp
if (moduleName == "kode:path") {
    args.GetReturnValue().Set(CreatePathModule(isolate, context));
    return;
}
```

Do not add `moduleName == "path"`.

Run: `make test-structured-runtime`

Expected: path tests pass except env/args tests.

- [ ] **Step 5: Implement `Kode.env` and `Kode.args`**

In `src/v8/engine.cc`, create native objects before or after `InstallKodeRuntimeBootstrap` and attach them to `Kode`:

```cpp
v8::Local<v8::Object> kode = context->Global()->Get(context, V8String(g_isolate, "Kode")).ToLocalChecked().As<v8::Object>();
```

Build `Kode.args` from `g_runtime_options`. Build `Kode.env` with native functions `get`, `has`, and `toObject` reading from a startup snapshot. Freeze arrays/objects using JS bootstrap or `Object.freeze` from V8.

Run: `make test-structured-runtime`

Expected: all host API tests pass.

## Task 3: Remove Parser Fallback

**Files:**
- Modify: `src/core/runtime.cc`
- Modify: `src/main.cc` only if command flow needs clear failure handling
- Modify: `Makefile`
- Create parser fallback tests listed above

- [ ] **Step 1: Write failing parser removal tests**

Create `tests/parser_fallback_syntax_error.js`:

```js
console.log("before syntax")
const broken =
console.log("after syntax")
```

Create `tests/parser_fallback_runtime_error.js`:

```js
throw new Error("runtime fallback should not parse")
```

Append checks:

```make
	output="$$(./bin/kode tests/parser_fallback_syntax_error.js 2>&1; printf ' status:%s' "$$?")"; case "$$output" in *"status:0"*|*"after syntax"*) printf '%s\n' "$$output"; exit 1;; *"parser_fallback_syntax_error.js"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/parser_fallback_runtime_error.js 2>&1; printf ' status:%s' "$$?")"; case "$$output" in *"status:0"*) printf '%s\n' "$$output"; exit 1;; *"parser_fallback_runtime_error.js"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(KODE_USE_V8=0 ./bin/kode tests/kode_no_process_global.js 2>&1; printf ' status:%s' "$$?")"; case "$$output" in *"status:0"*) printf '%s\n' "$$output"; exit 1;; *"V8 is required"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
```

Run: `make test-structured-runtime`

Expected: FAIL because V8 errors still fall through to parser and `KODE_USE_V8=0` still uses parser.

- [ ] **Step 2: Remove fallback from `ExecuteString`**

Replace `KodeRuntime::ExecuteString` with:

```cpp
bool KodeRuntime::ExecuteString(const std::string& source, const std::string& filename) {
    if (const char* env = std::getenv("KODE_USE_V8")) {
        if (std::string(env) == "0") {
            std::cerr << "V8 is required for JavaScript execution" << std::endl;
            return false;
        }
    }

    if (!kode::v8embed::available()) {
        std::cerr << "V8 is required for JavaScript execution" << std::endl;
        return false;
    }

    std::string err;
    std::string result = kode::v8embed::runScript(source, filename, &err);
    if (!err.empty()) {
        std::cerr << "[V8] Error: " << err << std::endl;
        return false;
    }
    if (!result.empty()) {
        std::cout << result << std::endl;
    }
    return true;
}
```

Run: `make test-structured-runtime`

Expected: parser fallback tests pass.

## Task 4: Integration And Verification

**Files:**
- Any touched files from Tasks 1-3

- [ ] **Step 1: Resolve overlaps**

Check `src/v8/engine.cc`, `src/core/runtime.cc`, `src/main.cc`, and `Makefile` for conflicting edits from subagents. Keep all tests and APIs from the spec.

- [ ] **Step 2: Run full verification**

Run:

```sh
make build && make test-v8-microtask && make test-structured-runtime && make test-http && make test-concurrency
```

Expected: all commands exit 0.

- [ ] **Step 3: Inspect status**

Run: `git status --short`

Expected: only intentional files from this plan are modified or untracked.

- [ ] **Step 4: Commit and push**

Stage only intentional files, then commit:

```sh
git commit -m "feat: polish runtime host apis"
git push
```

Expected: branch pushed successfully.
