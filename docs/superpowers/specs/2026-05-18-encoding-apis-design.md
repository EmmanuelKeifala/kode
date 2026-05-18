# Encoding APIs Design

## Goal

Add small UTF-8 byte/encoding primitives that support future binary APIs, including richer crypto inputs, without committing Kode to full Node or browser compatibility semantics.

## Scope

This milestone adds three API entry points backed by one core implementation:

- `require("kode:encoding")` as the canonical Kode-native module.
- `Kode.text` as a thin global convenience wrapper.
- `TextEncoder` and `TextDecoder` as Web-style compatibility wrappers.

All three entry points support UTF-8 only in this first slice.

Out of scope:

- non-UTF encodings.
- streaming decode.
- Web `TextDecoder` options such as `fatal` and `ignoreBOM`.
- full WHATWG encoding label compatibility.
- Node `Buffer` or bare `require("encoding")` compatibility.

## Canonical API

```js
const encoding = require("kode:encoding")

const bytes = encoding.encodeUtf8("hello")
console.log(bytes instanceof Uint8Array)
console.log(encoding.decodeUtf8(bytes))
```

`encoding.encodeUtf8(text)`:

- accepts a JavaScript string.
- returns a `Uint8Array` containing the UTF-8 bytes.

`encoding.decodeUtf8(bytes)`:

- accepts a `Uint8Array`, `ArrayBuffer`, or `DataView` if V8 support is straightforward.
- returns a JavaScript string decoded as UTF-8.
- replaces invalid UTF-8 sequences with the standard replacement character rather than adding a fatal mode in this slice.

Bare `require("encoding")` remains unsupported and should throw `EUNSUPPORTED_MODULE` from `module.require`.

## Convenience API

```js
const bytes = Kode.text.encode("hello")
console.log(Kode.text.decode(bytes))
```

`Kode.text.encode(text)` delegates to the same core UTF-8 encoder as `encoding.encodeUtf8`.

`Kode.text.decode(bytes)` delegates to the same core UTF-8 decoder as `encoding.decodeUtf8`.

`Kode.text` should be frozen with the rest of the `Kode` host surface.

## Web-Style API

```js
const bytes = new TextEncoder().encode("hello")
console.log(new TextDecoder().decode(bytes))
```

`TextEncoder`:

- is constructible with no options.
- exposes `encode(text)`.
- does not implement `encodeInto` in this slice.

`TextDecoder`:

- is constructible with no arguments or with `"utf-8"`.
- exposes `decode(bytes)`.
- does not implement streaming or option handling in this slice.

The constructors should be compatibility wrappers over Kode's core encoding helpers, not independent implementations.

## Architecture

Add a focused V8 built-in module:

- `src/v8/builtins/encoding.h`
  - declares `CreateEncodingModule`.
  - declares helper installers for `Kode.text` and text globals if they need to be shared from `kode_host.cc`.
- `src/v8/builtins/encoding.cc`
  - owns UTF-8 encode/decode helpers.
  - owns `kode:encoding` callbacks.
  - owns `Kode.text` object creation helpers.
  - owns `TextEncoder` and `TextDecoder` constructor/prototype setup.
- `src/v8/module_loader.cc`
  - dispatches only `"kode:encoding"` to `CreateEncodingModule`.
- `src/v8/kode_host.cc`
  - installs `Kode.text` and the global `TextEncoder`/`TextDecoder` wrappers during host API setup.
- `Makefile`
  - adds `src/v8/builtins/encoding.cc` to the app source list.

Keep the implementation small and dependency-free. Use V8 primitives for `Uint8Array`, `ArrayBuffer`, and UTF-8 string conversion where possible.

## Errors

Invalid argument types should throw structured Kode errors with `code: "EINVAL"` and operation names matching the public function:

- `kode:encoding.encodeUtf8`
- `kode:encoding.decodeUtf8`
- `Kode.text.encode`
- `Kode.text.decode`
- `TextEncoder.encode`
- `TextDecoder.decode`

Unsupported `TextDecoder` labels should throw a structured Kode error with `code: "EUNSUPPORTED_ENCODING"` and operation `"TextDecoder"`.

## Testing

Add smoke tests for:

- `require("kode:encoding").encodeUtf8("hello")` returns a `Uint8Array` with expected length and values.
- `require("kode:encoding").decodeUtf8(bytes)` returns the original string.
- Unicode round-trip, for example `"hé 👋"`.
- bare `require("encoding")` remains unsupported.
- `Kode.text.encode` and `Kode.text.decode` round-trip and `Kode.text` is protected from replacement.
- `new TextEncoder().encode("hello")` returns expected bytes.
- `new TextDecoder().decode(bytes)` returns expected string.
- unsupported decoder label throws `EUNSUPPORTED_ENCODING`.

Full verification command:

```sh
make build && make test-v8-microtask && make test-structured-runtime && make test-http && make test-concurrency
```

## Constraints

- One core implementation should back all three surfaces.
- Keep `kode:encoding` canonical; the other surfaces are wrappers.
- Do not introduce Node `Buffer`.
- Do not alias bare `encoding`.
- Do not expand `kode:crypto` in this slice.
