# Kode Crypto Built-In Design

## Goal

Add a small Kode-native crypto built-in that validates the new V8 built-in module boundaries and gives scripts a useful hashing primitive without adopting Node's `crypto` API or the full WebCrypto surface.

## Scope

This milestone adds only `require("kode:crypto")` with a synchronous `hash` function for SHA-256 string input.

It does not add:

- bare `require("crypto")` compatibility.
- WebCrypto globals such as `crypto.subtle`.
- encryption, signing, HMAC, key import/export, random bytes, or streaming hashes.
- binary input support beyond JavaScript strings.

## API

```js
const crypto = require("kode:crypto")

const digest = crypto.hash("sha256", "hello")
// {
//   algorithm: "sha256",
//   hex: "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"
// }
```

`hash(algorithm, data)` accepts:

- `algorithm`: string. The only supported value is `"sha256"`.
- `data`: string. The bytes hashed are the UTF-8 bytes of the JavaScript string.

It returns a plain object:

- `algorithm`: normalized algorithm name, currently `"sha256"`.
- `hex`: lowercase hexadecimal digest string.

## Architecture

Add a focused built-in module:

- `src/v8/builtins/crypto.h`
  - declares `CreateCryptoModule(v8::Isolate*, v8::Local<v8::Context>)`.
- `src/v8/builtins/crypto.cc`
  - implements argument validation, SHA-256 hashing, hex encoding, and module object creation.
- `src/v8/module_loader.cc`
  - includes `builtins/crypto.h` and dispatches only `"kode:crypto"` to `CreateCryptoModule`.
- `Makefile`
  - adds `src/v8/builtins/crypto.cc` to `APP`.

The implementation should keep the SHA-256 code local to `crypto.cc` for this slice. Avoid adding OpenSSL or another dependency until the crypto surface needs broader algorithm or key support.

## Errors

Invalid argument types should follow existing V8 helper behavior through `ReadStringArg`, producing structured `EINVAL` errors with operation `"kode:crypto.hash"`.

Unsupported algorithms should throw a structured Kode error:

```js
{
  code: "EUNSUPPORTED_ALGORITHM",
  operation: "kode:crypto.hash"
}
```

Bare `require("crypto")` must continue to fail as an unsupported module:

```js
{
  code: "EUNSUPPORTED_MODULE",
  operation: "module.require"
}
```

## WebCrypto Notes

WebCrypto's `SubtleCrypto.digest()` is useful but intentionally out of scope. It returns `Promise<ArrayBuffer>` and accepts `ArrayBuffer`, typed arrays, or `DataView`; ergonomic string hashing usually also needs `TextEncoder` or another byte API. Adding that now would expand Kode's platform commitments beyond the purpose of this milestone.

The `kode:crypto.hash()` API keeps the first crypto slice small while leaving room to add `kode:crypto.subtle.digest()` later when Kode has clearer byte and encoding primitives.

## Testing

Add smoke tests for:

- `crypto.hash("sha256", "hello")` returns the known SHA-256 hex digest.
- unsupported algorithms throw `EUNSUPPORTED_ALGORITHM` with operation `"kode:crypto.hash"`.
- bare `require("crypto")` remains unsupported.

Run full verification:

```sh
make build && make test-v8-microtask && make test-structured-runtime && make test-http && make test-concurrency
```

## Constraints

- Keep the API Kode-native and explicit.
- Do not introduce Node compatibility aliases.
- Do not add global `crypto` yet.
- Keep implementation isolated to the new built-in and module loader registration.
- Prefer the smallest correct implementation over a generalized crypto abstraction.
