# Local CommonJS Modules Design

## Goal

Kode should move beyond hardcoded built-in modules and support small multi-file programs. The next phase adds local CommonJS-style modules without trying to clone Node's full package loader.

Initial target:

```js
// math.js
exports.add = (a, b) => a + b

// app.js
const math = require("./math.js")
console.log(math.add(2, 3))
```

## Principles

- Keep the first module system small and understandable.
- Support local project files before packages.
- Keep `kode:*` names reserved for built-in modules.
- Cache modules by normalized absolute path.
- Make module globals explicit: `exports`, `module`, `__filename`, `__dirname`, and `require`.
- Do not implement npm resolution, package.json, or ESM in this phase.

## API Surface

### Local Require

Supported:

```js
const util = require("./util.js")
const nested = require("./lib/nested.js")
```

Rules:

- Local specifiers must start with `./` or `../`.
- Paths resolve relative to the requiring module's directory.
- `.js` extension is required in this phase.
- The resolved path is normalized before cache lookup.
- Missing files throw a structured error.

Unsupported in this phase:

```js
require("lodash")
require("./dir")
require("./dir/index.js") // only works if spelled exactly
import { x } from "./x.js"
```

### Built-In Require

Built-ins remain available through reserved names:

```js
const fs = require("kode:fs")
```

Rules:

- `kode:*` specifiers are handled by the built-in module table.
- `fs` may continue to work as a legacy alias for now.
- Local files cannot override `kode:*` built-ins.

## Module Execution Model

Each local module is wrapped and executed with CommonJS-like bindings:

```js
(function (exports, require, module, __filename, __dirname) {
  // source code
})
```

Rules:

- `module.exports` starts as the same object as `exports`.
- Assigning properties to `exports` works.
- Reassigning `module.exports` works.
- The return value of `require(...)` is `module.exports`.
- `__filename` is the normalized absolute module path.
- `__dirname` is the normalized absolute parent directory.

## Caching

Modules are cached before execution completes.

Rules:

- Cache key is normalized absolute path.
- Requiring the same module twice returns the same exports object.
- Caching before execution prevents infinite recursion for simple cycles.
- Full cycle semantics are not polished in this phase, but basic self-consistency is required.

Example:

```js
const a = require("./counter.js")
const b = require("./counter.js")
console.log(a === b) // true
```

## Error Model

Local module errors are JavaScript `Error` objects with stable fields.

Missing module:

```js
try {
  require("./missing.js")
} catch (err) {
  console.log(err.code)      // "EMODULE_NOT_FOUND"
  console.log(err.operation) // "module.require"
  console.log(err.path)      // resolved attempted path
}
```

Unsupported specifier:

```js
try {
  require("lodash")
} catch (err) {
  console.log(err.code) // "EUNSUPPORTED_MODULE"
}
```

Rules:

- Syntax/runtime errors from the module body propagate normally.
- Missing file errors are structured as `EMODULE_NOT_FOUND`.
- Unsupported specifiers are structured as `EUNSUPPORTED_MODULE`.
- Error `operation` is `"module.require"`.

## Native Implementation Direction

The current V8 embedder has a single global context and a `RequireCallback` that returns built-ins. This phase should add local-file module loading inside that existing embedder without broad restructuring.

Implementation direction:

- Add a module cache in `src/v8/engine.cc` keyed by normalized absolute path.
- Track the current executing module directory during module execution.
- Split require handling into built-in and local paths.
- Read local JS files from disk using C++ file I/O for module source loading.
- Compile the wrapper function in the existing context.
- Execute the wrapper with the module-local bindings.
- Update `runScript` so top-level file execution has a filename context when called from `KodeRuntime::ExecuteFile`.

The first implementation can keep this in `src/v8/engine.cc`. A later cleanup can extract a module loader file once behavior stabilizes.

## Testing Strategy

Add V8-level smoke tests through `./bin/kode` and `make test-structured-runtime`.

Required tests:

- `require("./module.js")` returns exported properties.
- `module.exports = ...` works.
- Module cache returns the same object for repeated require.
- Nested module paths resolve relative to the requiring module.
- `__filename` and `__dirname` are available.
- Missing local module throws `EMODULE_NOT_FOUND`.
- Unsupported bare specifier throws `EUNSUPPORTED_MODULE`.
- Existing built-in module tests keep passing.

Full verification:

```bash
make build && make test-v8-microtask && make test-structured-runtime && make test-http && make test-concurrency
```

## Scope Boundaries

In scope:

- `require("./file.js")`
- `require("../file.js")`
- `exports` object mutation
- `module.exports` reassignment
- Module cache
- Relative resolution from requiring module
- Structured module load errors

Out of scope:

- ES modules
- npm/package resolution
- `package.json`
- directory index resolution unless path is explicit
- JSON modules
- Native addons
- Source maps
- Hot reload
