# Runtime Polish And Host APIs Design

## Goal

Complete one coordinated runtime milestone that improves local CommonJS loading, adds small Kode-native host APIs, and removes the legacy parser fallback from normal JavaScript execution.

## Scope

This milestone has three implementation lanes plus integration:

- Require polish: circular dependency coverage, optional `.js` probing, and filename-aware errors.
- Kode host APIs: `kode:path`, read-only `Kode.env`, and read-only `Kode.args`.
- Parser fallback removal: V8 becomes the only JavaScript execution path.
- Integration: resolve overlaps, verify the full suite, review, commit, and push.

There are no unrelated dirty changes at design time. If unrelated changes appear during implementation, keep them out of the feature commit unless explicitly requested.

## Architecture

V8 is the only real JavaScript execution path. `KodeRuntime::ExecuteString` should call V8 and return failure on V8 compile/runtime errors instead of falling through to the legacy parser. Parser source may remain in the repository if still needed by build or future cleanup, but runtime execution must not call it after V8 errors.

Built-ins continue to live in `src/v8/engine.cc` for this milestone. This keeps the change small and consistent with the current `kode:fs` and local CommonJS loader implementation. If the built-in surface grows later, it can be split into dedicated files.

Implementation work should be parallelized with subagents:

- Agent A: require polish and module tests.
- Agent B: `kode:path`, `Kode.env`, `Kode.args`, and host API tests.
- Agent C: parser fallback removal and failure-mode tests.
- Main agent: integration, conflict resolution, verification, review, commits, and push.

## Require Polish

Local module resolution supports both explicit and extensionless JavaScript files:

- `require("./math.js")` keeps working.
- `require("./math")` resolves the literal path first if it exists, then tries `./math.js`.
- `require("../math")` follows the same rule from the requiring module directory.
- The module cache key is always the final normalized absolute resolved path.

Circular dependencies keep CommonJS-style partial export behavior. The loader caches the initial `exports` object before module execution starts, so a cycle receives that in-progress object instead of recursing forever. If a module later reassigns `module.exports`, any dependent that already captured the original `exports` object keeps that original object. This behavior should be documented by tests rather than changed in this milestone.

Compile and runtime errors should identify the relevant source file:

- Top-level script syntax/runtime errors mention the entry filename.
- Module syntax/runtime errors mention the required module filename, not only the entry script.
- Wrapper-related line offsets should not hide which module failed. Exact line/column precision is useful but less important than reliably naming the file.

Structured module errors remain stable:

- Missing local modules throw `EMODULE_NOT_FOUND` with `operation: "module.require"` and the attempted resolved path.
- Unsupported non-`kode:*` bare modules throw `EUNSUPPORTED_MODULE` with `operation: "module.require"`.

## Kode Host APIs

Kode host APIs stay explicit and Kode-native. Do not add Node-style `process`, `process.env`, `process.argv`, or a bare `require("path")` alias.

### `kode:path`

Use:

```js
const path = require("kode:path")
```

Initial API:

- `path.join(...parts)`
- `path.normalize(path)`
- `path.dirname(path)`
- `path.basename(path)`
- `path.extname(path)`
- `path.isAbsolute(path)`
- `path.resolve(path)`

Path functions are synchronous, side-effect-free string/path transforms. They do not check file existence. Results should use slash-normalized strings via `std::filesystem::path::generic_string()`.

### `Kode.env`

`Kode.env` is a read-only startup snapshot of environment variables:

```js
Kode.env.get("HOME")
Kode.env.has("HOME")
Kode.env.toObject()
```

Behavior:

- `get(name)` returns the variable value or `undefined`.
- `has(name)` returns a boolean.
- `toObject()` returns a plain object snapshot.
- The API does not mutate host environment variables.

### `Kode.args`

`Kode.args` is a read-only object that exposes runtime invocation data without a `process` global:

```js
Kode.args.executable
Kode.args.script
Kode.args.values
```

Behavior:

- `executable` is the path used to launch Kode.
- `script` is the script filename for file execution, or `undefined` for `-e` execution.
- `values` is a frozen array of user arguments after the script filename or after `-e <code>`.

Command-line parsing should preserve user script arguments instead of treating every non-option as a separate script.

## Parser Fallback Removal

`ExecuteString` should fail when V8 fails. It should not parse and execute the same source with the legacy parser after a V8 error.

`KODE_USE_V8=0` should no longer silently run parser mode. It should fail clearly because V8 is the required JavaScript engine for normal runtime execution. A future demo or legacy parser binary can be designed separately if needed.

## Testing

Add smoke tests to `test-structured-runtime`.

Require polish tests:

- `require("./math")` resolves `math.js`.
- Circular dependencies do not recurse forever and expose partial exports.
- Nested modules can use extensionless `require("../math")`.
- Module syntax/runtime errors mention the failing module filename.

Host API tests:

- `kode:path` methods return expected slash-normalized strings.
- Bare `require("path")` throws `EUNSUPPORTED_MODULE`.
- `Kode.env` reads a known environment variable, reports missing variables, and returns a snapshot object.
- `Kode.args` exposes script filename and user args.
- `typeof process === "undefined"`.

Parser removal tests:

- V8 syntax errors fail command execution instead of falling through to parser output.
- V8 runtime errors fail command execution instead of falling through to parser output.
- `KODE_USE_V8=0 ./bin/kode ...` fails clearly.

Full verification command:

```sh
make build && make test-v8-microtask && make test-structured-runtime && make test-http && make test-concurrency
```

## Commit Strategy

Use one implementation commit for this coordinated milestone unless unrelated dirty changes appear. Keep unrelated changes out of the feature commit. If unrelated changes need to be preserved, commit them separately only with explicit approval.
