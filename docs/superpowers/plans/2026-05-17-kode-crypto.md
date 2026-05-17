# Kode Crypto Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `require("kode:crypto").hash("sha256", data)` with structured errors and no bare `crypto` alias.

**Architecture:** Add a focused V8 built-in in `src/v8/builtins/crypto.cc` and register it from the CommonJS module loader. Keep SHA-256 local to this built-in and avoid new external dependencies.

**Tech Stack:** C++17, V8 embedder APIs, existing Kode error helpers, Makefile smoke tests.

---

### Task 1: Crypto Smoke Tests

**Files:**
- Create: `tests/kode_crypto_hash_sha256.js`
- Create: `tests/kode_crypto_hash_unsupported.js`
- Create: `tests/kode_crypto_no_bare_alias.js`
- Modify: `Makefile`

- [ ] **Step 1: Add SHA-256 success smoke test**

```js
const crypto = require("kode:crypto")
const digest = crypto.hash("sha256", "hello")
console.log("crypto-sha256", digest.algorithm, digest.hex)
```

- [ ] **Step 2: Add unsupported algorithm smoke test**

```js
const crypto = require("kode:crypto")
try {
  crypto.hash("sha1", "hello")
} catch (err) {
  console.log("crypto-unsupported", err.code, err.operation)
}
```

- [ ] **Step 3: Add no bare alias smoke test**

```js
try {
  require("crypto")
} catch (err) {
  console.log("crypto-no-bare", err.code, err.operation)
}
```

- [ ] **Step 4: Add tests to `test-structured-runtime`**

Add these checks to `Makefile` under `test-structured-runtime`:

```make
	./$(OUT) tests/kode_crypto_hash_sha256.js | grep "crypto-sha256 sha256 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"
	./$(OUT) tests/kode_crypto_hash_unsupported.js | grep "crypto-unsupported EUNSUPPORTED_ALGORITHM kode:crypto.hash"
	./$(OUT) tests/kode_crypto_no_bare_alias.js | grep "crypto-no-bare EUNSUPPORTED_MODULE module.require"
```

- [ ] **Step 5: Run failing tests**

Run: `make build && make test-structured-runtime`

Expected: FAIL because `kode:crypto` is unsupported.

### Task 2: Crypto Built-In

**Files:**
- Create: `src/v8/builtins/crypto.h`
- Create: `src/v8/builtins/crypto.cc`
- Modify: `src/v8/module_loader.cc`
- Modify: `Makefile`

- [ ] **Step 1: Add crypto module declaration**

```cpp
#pragma once

#include <v8.h>

namespace kode { namespace v8embed {

v8::Local<v8::Object> CreateCryptoModule(v8::Isolate* isolate, v8::Local<v8::Context> context);

} } // namespace kode::v8embed
```

- [ ] **Step 2: Add SHA-256 implementation and callback**

Implement `src/v8/builtins/crypto.cc` with a local SHA-256 function, lowercase hex encoder, `HashCallback`, and `CreateCryptoModule`. `HashCallback` must read two strings with `ReadStringArg(..., "kode:crypto.hash", ...)`, reject every algorithm except `sha256` with `CreateKodeError(..., "EUNSUPPORTED_ALGORITHM", ..., "kode:crypto.hash", algorithm)`, and return `{ algorithm: "sha256", hex }`.

- [ ] **Step 3: Register only `kode:crypto`**

In `src/v8/module_loader.cc`, include `builtins/crypto.h` and add:

```cpp
} else if (moduleName == "kode:crypto") {
    args.GetReturnValue().Set(CreateCryptoModule(isolate, context));
```

Do not add a bare `crypto` branch.

- [ ] **Step 4: Add source to `Makefile`**

Add `src/v8/builtins/crypto.cc` to the `APP` source list.

- [ ] **Step 5: Run focused verification**

Run: `make build && make test-structured-runtime`

Expected: PASS.

### Task 3: Full Verification And Commit

**Files:**
- Verify all touched files.

- [ ] **Step 1: Run full verification**

Run: `make build && make test-v8-microtask && make test-structured-runtime && make test-http && make test-concurrency`

Expected: PASS.

- [ ] **Step 2: Inspect diff**

Run: `git status --short && git diff`

Expected: only crypto plan, tests, built-in, module loader, and Makefile changes.

- [ ] **Step 3: Commit implementation**

```bash
git add -f docs/superpowers/plans/2026-05-17-kode-crypto.md
git add Makefile src/v8/module_loader.cc src/v8/builtins/crypto.h src/v8/builtins/crypto.cc tests/kode_crypto_hash_sha256.js tests/kode_crypto_hash_unsupported.js tests/kode_crypto_no_bare_alias.js
git commit -m "feat: add kode crypto hash"
```
