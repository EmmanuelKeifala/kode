# Kode Native Filesystem Design

## Goal

Kode's filesystem API should not mirror Node's historical `fs` surface. It should be small, explicit, promise-first, and designed around structured runtime ownership.

The next phase builds a Kode-native filesystem surface on top of V8 and `ModernFS`:

```js
const fs = require("kode:fs")

await Kode.scope(async (scope) => {
  const file = await scope.async(() =>
    fs.read("config.json", { as: "text" })
  )

  await scope.async(() =>
    fs.write("out/result.json", file.text, { create: "parents" })
  )

  const info = await scope.async(() =>
    fs.info("out/result.json")
  )

  console.log(info.kind, info.size)
})
```

## Principles

- Promise-first APIs are the primary surface.
- No callback API is added for new behavior.
- No sync API is added in this phase.
- No overloaded argument soup.
- Text vs bytes is explicit.
- Parent directory creation is explicit.
- Missing-file behavior depends on operation semantics.
- Errors are structured and stable.
- The option objects reserve `signal` for a future cancellation phase without changing the core call shapes.

## API Surface

This phase adds three Kode-native APIs under `require("kode:fs")`.

### `fs.read(path, options)`

Initial supported shape:

```js
const file = await fs.read("notes.txt", { as: "text" })
console.log(file.text)
console.log(file.info.kind)
```

Rules:

- `path` must be a string.
- `options` is optional.
- `options.as` defaults to `"text"` in this phase.
- Only `"text"` is supported in this phase.
- Missing files reject with `ENOENT`.
- Success returns `{ text, info }`.

Future-compatible shape:

```js
await fs.read("image.png", { as: "bytes", signal })
```

Bytes are intentionally out of scope for this phase.

### `fs.write(path, data, options)`

Initial supported shape:

```js
const result = await fs.write("out/notes.txt", "hello", {
  create: "parents",
})
console.log(result.bytesWritten)
```

Rules:

- `path` must be a string.
- `data` must be a string in this phase.
- `options` is optional.
- `options.create` defaults to `"none"`.
- `options.create: "parents"` creates missing parent directories.
- Missing parent directories reject unless `create` is `"parents"`.
- Success returns `{ bytesWritten, info }`.

Future-compatible shape:

```js
await fs.write("out/blob.bin", bytes, { create: "parents", signal })
```

Bytes are intentionally out of scope for this phase.

### `fs.info(path)`

Initial supported shape:

```js
const info = await fs.info("out/notes.txt")
if (info) {
  console.log(info.kind, info.size)
}
```

Rules:

- `path` must be a string.
- Missing paths resolve to `null`.
- Existing paths resolve to an info object.
- `info()` does not reject for ordinary missing files.
- `info()` rejects for invalid arguments or unexpected host failures.

This replaces a Node-style `exists()` as the preferred primitive. Users can write:

```js
const exists = (await fs.info("path.txt")) !== null
```

## Info Object

The info object should be stable and simple:

```js
{
  path: "out/notes.txt",
  kind: "file",
  size: 5,
  mimeType: "text/plain",
  lastModified: 1778390000000,
}
```

Rules:

- `kind` is `"file"`, `"directory"`, or `"other"`.
- `size` is bytes for files and `0` for directories.
- `mimeType` is present for files when known.
- `lastModified` is epoch milliseconds when available.

## Error Model

All rejected host errors should be JavaScript `Error` objects with stable fields:

```js
try {
  await fs.read("missing.txt", { as: "text" })
} catch (err) {
  console.log(err.code)      // "ENOENT"
  console.log(err.operation) // "fs.read"
  console.log(err.path)      // "missing.txt"
}
```

Initial codes:

- `EINVAL`: invalid argument shape or unsupported option.
- `ENOENT`: missing path when the operation requires it.
- `EIO`: unexpected host I/O failure.

Rules:

- Error `operation` is `"fs.read"`, `"fs.write"`, or `"fs.info"`.
- Path-based errors include `path`.
- Missing `info()` result is `null`, not an error.

## Compatibility Aliases

The existing `fs.readText(path)` should remain for now as an alias over:

```js
fs.read(path, { as: "text" }).then((file) => file.text)
```

This avoids breaking the first structured-runtime smoke tests while the Kode-native API becomes the primary shape.

No new Node-style aliases should be added.

## Native Implementation Direction

The implementation should use `ModernFS` as the main C++ filesystem implementation.

Targeted internal cleanup for this phase:

- Add an explicit operation enum to `ModernFS::AsyncOperation`.
- Route `read`, `write`, and `info` by operation enum.
- Keep each operation responsible for only its result fields.
- Add a write path option that controls parent directory creation.
- Keep legacy `KodeFS` untouched except where existing code already calls it.

This reduces callback-field inference in `ModernFS::workCallback` without broad refactoring.

## Testing Strategy

Add V8-level smoke tests through `./bin/kode` and `make test-structured-runtime`.

Required tests:

- `fs.read(path, { as: "text" })` returns `{ text, info }`.
- `fs.read()` rejects missing files with `ENOENT`, `operation: "fs.read"`, and `path`.
- `fs.write(path, data)` rejects when parents are missing.
- `fs.write(path, data, { create: "parents" })` writes data and returns `{ bytesWritten, info }`.
- `fs.info(path)` returns info for existing files.
- `fs.info(path)` returns `null` for missing files.
- `fs.readText(path)` continues to return text.

Keep the existing verification set passing:

```bash
make build && make test-v8-microtask && make test-structured-runtime && make test-http && make test-concurrency
```

## Scope Boundaries

In scope:

- `fs.read`
- `fs.write`
- `fs.info`
- `fs.readText` compatibility alias
- Structured errors for the new APIs
- Minimal `ModernFS` operation dispatch cleanup

Out of scope:

- ES module imports
- Binary/bytes support
- Streams
- Watchers
- Permissions
- Full cancellation with `{ signal }`
- Node-compatible callback APIs
- Removing parser filesystem behavior
