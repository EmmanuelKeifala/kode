# Encoding APIs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add UTF-8 encoding APIs through `kode:encoding`, `Kode.text`, and `TextEncoder`/`TextDecoder`, all backed by one V8 implementation.

**Architecture:** Implement one focused built-in file, `src/v8/builtins/encoding.cc`, that owns UTF-8 encode/decode helpers and creates all three API surfaces. Register `kode:encoding` in the module loader and install `Kode.text` plus global constructors from `kode_host.cc`.

**Tech Stack:** C++20, V8 embedder APIs, existing Kode structured error helpers, Makefile smoke tests.

---

### Task 1: Canonical `kode:encoding` Module

**Files:**
- Create: `src/v8/builtins/encoding.h`
- Create: `src/v8/builtins/encoding.cc`
- Modify: `src/v8/module_loader.cc`
- Modify: `Makefile`
- Create: `tests/kode_encoding_basic.js`
- Create: `tests/kode_encoding_unicode.js`
- Create: `tests/kode_encoding_no_bare_alias.js`

- [ ] **Step 1: Add failing tests**

Create `tests/kode_encoding_basic.js`:

```js
const encoding = require("kode:encoding")
const bytes = encoding.encodeUtf8("hello")
console.log("encoding-basic", bytes instanceof Uint8Array, bytes.length, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], encoding.decodeUtf8(bytes))
```

Create `tests/kode_encoding_unicode.js`:

```js
const encoding = require("kode:encoding")
const text = "hé 👋"
const bytes = encoding.encodeUtf8(text)
console.log("encoding-unicode", bytes instanceof Uint8Array, bytes.length, encoding.decodeUtf8(bytes) === text)
```

Create `tests/kode_encoding_no_bare_alias.js`:

```js
try {
  require("encoding")
} catch (err) {
  console.log("encoding-no-bare", err.code, err.operation)
}
```

Add these checks to `Makefile` under `test-structured-runtime` near the `kode:path`/`kode:crypto` checks:

```make
	output="$$(./bin/kode tests/kode_encoding_basic.js)"; case "$$output" in *"encoding-basic true 5 104 101 108 108 111 hello"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_encoding_unicode.js)"; case "$$output" in *"encoding-unicode true 8 true"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_encoding_no_bare_alias.js)"; case "$$output" in *"encoding-no-bare EUNSUPPORTED_MODULE module.require"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
```

- [ ] **Step 2: Verify tests fail**

Run: `make build && make test-structured-runtime`

Expected: FAIL with unsupported module `kode:encoding`.

- [ ] **Step 3: Add `encoding.h` declaration**

```cpp
#pragma once

#include <v8.h>

namespace kode { namespace v8embed {

v8::Local<v8::Object> CreateEncodingModule(v8::Isolate* isolate, v8::Local<v8::Context> context);
bool InstallTextEncodingGlobals(v8::Isolate* isolate, v8::Local<v8::Context> context);
bool InstallKodeTextApi(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> kode);

} } // namespace kode::v8embed
```

- [ ] **Step 4: Add minimal `kode:encoding` implementation**

Implement `src/v8/builtins/encoding.cc` with:

```cpp
#include "encoding.h"

#include "../v8_helpers.h"

#include <string>
#include <vector>

namespace kode { namespace v8embed {

namespace {

bool ReadByteInput(v8::Isolate* isolate,
                   v8::Local<v8::Context> context,
                   v8::Local<v8::Value> value,
                   const std::string& operation,
                   std::vector<uint8_t>* out) {
    if (value->IsUint8Array()) {
        v8::Local<v8::Uint8Array> view = value.As<v8::Uint8Array>();
        v8::Local<v8::ArrayBuffer> buffer = view->Buffer();
        const uint8_t* data = static_cast<const uint8_t*>(buffer->Data()) + view->ByteOffset();
        out->assign(data, data + view->ByteLength());
        return true;
    }
    if (value->IsArrayBuffer()) {
        v8::Local<v8::ArrayBuffer> buffer = value.As<v8::ArrayBuffer>();
        const uint8_t* data = static_cast<const uint8_t*>(buffer->Data());
        out->assign(data, data + buffer->ByteLength());
        return true;
    }
    if (value->IsDataView()) {
        v8::Local<v8::DataView> view = value.As<v8::DataView>();
        v8::Local<v8::ArrayBuffer> buffer = view->Buffer();
        const uint8_t* data = static_cast<const uint8_t*>(buffer->Data()) + view->ByteOffset();
        out->assign(data, data + view->ByteLength());
        return true;
    }
    isolate->ThrowException(CreateKodeError(isolate, context,
        "EINVAL", operation + " requires bytes", operation, ""));
    return false;
}

v8::Local<v8::Uint8Array> EncodeUtf8(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     const std::string& text) {
    v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, text.size());
    if (!text.empty()) {
        std::memcpy(buffer->Data(), text.data(), text.size());
    }
    return v8::Uint8Array::New(buffer, 0, text.size());
}

v8::Local<v8::String> DecodeUtf8(v8::Isolate* isolate, const std::vector<uint8_t>& bytes) {
    return v8::String::NewFromUtf8(isolate,
        reinterpret_cast<const char*>(bytes.data()),
        v8::NewStringType::kNormal,
        static_cast<int>(bytes.size())).ToLocalChecked();
}

void EncodeUtf8Callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string text;
    if (!ReadStringArg(isolate, context, args, 0, "kode:encoding.encodeUtf8", &text)) return;
    args.GetReturnValue().Set(EncodeUtf8(isolate, context, text));
}

void DecodeUtf8Callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::vector<uint8_t> bytes;
    if (args.Length() < 1 || !ReadByteInput(isolate, context, args[0], "kode:encoding.decodeUtf8", &bytes)) return;
    args.GetReturnValue().Set(DecodeUtf8(isolate, bytes));
}

} // namespace

v8::Local<v8::Object> CreateEncodingModule(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::Local<v8::Object> encoding = v8::Object::New(isolate);
    encoding->Set(context, V8String(isolate, "encodeUtf8"), v8::Function::New(context, EncodeUtf8Callback).ToLocalChecked()).FromMaybe(false);
    encoding->Set(context, V8String(isolate, "decodeUtf8"), v8::Function::New(context, DecodeUtf8Callback).ToLocalChecked()).FromMaybe(false);
    return encoding;
}

bool InstallTextEncodingGlobals(v8::Isolate*, v8::Local<v8::Context>) { return true; }
bool InstallKodeTextApi(v8::Isolate*, v8::Local<v8::Context>, v8::Local<v8::Object>) { return true; }

} } // namespace kode::v8embed
```

Also include `<cstring>` for `std::memcpy`.

- [ ] **Step 5: Register module and build source**

In `src/v8/module_loader.cc`, include `builtins/encoding.h` and add:

```cpp
} else if (moduleName == "kode:encoding") {
    args.GetReturnValue().Set(CreateEncodingModule(isolate, context));
```

Add `src/v8/builtins/encoding.cc` to the `APP` source list in `Makefile`.

- [ ] **Step 6: Verify Task 1 passes**

Run: `make build && make test-structured-runtime`

Expected: PASS for the new `kode:encoding` tests and existing runtime smoke tests.

- [ ] **Step 7: Commit Task 1**

```bash
git add Makefile src/v8/module_loader.cc src/v8/builtins/encoding.h src/v8/builtins/encoding.cc tests/kode_encoding_basic.js tests/kode_encoding_unicode.js tests/kode_encoding_no_bare_alias.js
git commit -m "feat: add kode encoding module"
```

### Task 2: `Kode.text` Wrapper

**Files:**
- Modify: `src/v8/builtins/encoding.cc`
- Modify: `src/v8/kode_host.cc`
- Modify: `Makefile`
- Create: `tests/kode_text_basic.js`

- [ ] **Step 1: Add failing `Kode.text` test**

Create `tests/kode_text_basic.js`:

```js
const original = Kode.text
try {
  Kode.text = { broken: true }
} catch (err) {}

const bytes = Kode.text.encode("hello")
console.log("kode-text", Kode.text === original, bytes instanceof Uint8Array, Kode.text.decode(bytes))
```

Add this check to `Makefile` under `test-structured-runtime` near the other host API tests:

```make
	output="$$(./bin/kode tests/kode_text_basic.js)"; case "$$output" in *"kode-text true true hello"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
```

- [ ] **Step 2: Verify test fails**

Run: `make build && make test-structured-runtime`

Expected: FAIL because `Kode.text` is undefined.

- [ ] **Step 3: Add operation-specific wrapper callbacks**

In `src/v8/builtins/encoding.cc`, factor encode/decode callbacks through helper functions that take operation strings:

```cpp
void EncodeWithOperation(const v8::FunctionCallbackInfo<v8::Value>& args, const std::string& operation) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string text;
    if (!ReadStringArg(isolate, context, args, 0, operation, &text)) return;
    args.GetReturnValue().Set(EncodeUtf8(isolate, context, text));
}

void DecodeWithOperation(const v8::FunctionCallbackInfo<v8::Value>& args, const std::string& operation) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::vector<uint8_t> bytes;
    if (args.Length() < 1 || !ReadByteInput(isolate, context, args[0], operation, &bytes)) return;
    args.GetReturnValue().Set(DecodeUtf8(isolate, bytes));
}
```

Then have the existing `kode:encoding` callbacks call these helpers with `kode:encoding.encodeUtf8` and `kode:encoding.decodeUtf8`.

- [ ] **Step 4: Add `Kode.text` callbacks and installer**

In `src/v8/builtins/encoding.cc`, add:

```cpp
void KodeTextEncodeCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    EncodeWithOperation(args, "Kode.text.encode");
}

void KodeTextDecodeCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    DecodeWithOperation(args, "Kode.text.decode");
}

bool InstallKodeTextApi(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> kode) {
    v8::Local<v8::Object> text = v8::Object::New(isolate);
    text->Set(context, V8String(isolate, "encode"), v8::Function::New(context, KodeTextEncodeCallback).ToLocalChecked()).FromMaybe(false);
    text->Set(context, V8String(isolate, "decode"), v8::Function::New(context, KodeTextDecodeCallback).ToLocalChecked()).FromMaybe(false);
    FreezeValue(context, text);
    return kode->Set(context, V8String(isolate, "text"), text).FromMaybe(false);
}
```

- [ ] **Step 5: Install `Kode.text` from host setup**

In `src/v8/kode_host.cc`, include `builtins/encoding.h` and call `InstallKodeTextApi(isolate, context, kode)` before `FreezeValue(context, kode)`:

```cpp
    if (!InstallKodeTextApi(isolate, context, kode)) return false;
```

- [ ] **Step 6: Verify Task 2 passes**

Run: `make build && make test-structured-runtime`

Expected: PASS.

- [ ] **Step 7: Commit Task 2**

```bash
git add Makefile src/v8/builtins/encoding.cc src/v8/kode_host.cc tests/kode_text_basic.js
git commit -m "feat: add Kode text encoding helpers"
```

### Task 3: `TextEncoder` And `TextDecoder` Wrappers

**Files:**
- Modify: `src/v8/builtins/encoding.cc`
- Modify: `src/v8/kode_host.cc`
- Modify: `Makefile`
- Create: `tests/text_encoder_decoder_basic.js`
- Create: `tests/text_decoder_unsupported_label.js`

- [ ] **Step 1: Add failing Web-style wrapper tests**

Create `tests/text_encoder_decoder_basic.js`:

```js
const bytes = new TextEncoder().encode("hello")
const text = new TextDecoder().decode(bytes)
console.log("text-encoder-decoder", bytes instanceof Uint8Array, bytes.length, bytes[0], bytes[4], text)
```

Create `tests/text_decoder_unsupported_label.js`:

```js
try {
  new TextDecoder("latin1")
} catch (err) {
  console.log("text-decoder-label", err.code, err.operation)
}
```

Add these checks to `Makefile` under `test-structured-runtime` near `tests/kode_text_basic.js`:

```make
	output="$$(./bin/kode tests/text_encoder_decoder_basic.js)"; case "$$output" in *"text-encoder-decoder true 5 104 111 hello"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/text_decoder_unsupported_label.js)"; case "$$output" in *"text-decoder-label EUNSUPPORTED_ENCODING TextDecoder"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
```

- [ ] **Step 2: Verify tests fail**

Run: `make build && make test-structured-runtime`

Expected: FAIL because `TextEncoder` and `TextDecoder` are undefined.

- [ ] **Step 3: Add constructor callbacks**

In `src/v8/builtins/encoding.cc`, add callbacks:

```cpp
void TextEncoderConstructor(const v8::FunctionCallbackInfo<v8::Value>& args) {
    if (!args.IsConstructCall()) {
        v8::Isolate* isolate = args.GetIsolate();
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EINVAL", "TextEncoder requires new", "TextEncoder", ""));
    }
}

void TextDecoderConstructor(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!args.IsConstructCall()) {
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EINVAL", "TextDecoder requires new", "TextDecoder", ""));
        return;
    }
    if (args.Length() > 0 && !args[0]->IsUndefined()) {
        std::string label;
        if (!ReadStringArg(isolate, context, args, 0, "TextDecoder", &label)) return;
        if (label != "utf-8" && label != "utf8") {
            isolate->ThrowException(CreateKodeError(isolate, context,
                "EUNSUPPORTED_ENCODING", "Unsupported encoding '" + label + "'", "TextDecoder", label));
        }
    }
}
```

- [ ] **Step 4: Add prototype methods and installer**

In `src/v8/builtins/encoding.cc`, add:

```cpp
void TextEncoderEncodeCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    EncodeWithOperation(args, "TextEncoder.encode");
}

void TextDecoderDecodeCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    DecodeWithOperation(args, "TextDecoder.decode");
}

bool InstallTextEncodingGlobals(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::Local<v8::FunctionTemplate> encoder_template = v8::FunctionTemplate::New(isolate, TextEncoderConstructor);
    encoder_template->SetClassName(V8String(isolate, "TextEncoder"));
    encoder_template->InstanceTemplate()->SetInternalFieldCount(0);
    encoder_template->PrototypeTemplate()->Set(isolate, "encode", v8::FunctionTemplate::New(isolate, TextEncoderEncodeCallback));
    v8::Local<v8::Function> encoder;
    if (!encoder_template->GetFunction(context).ToLocal(&encoder)) return false;
    if (!context->Global()->Set(context, V8String(isolate, "TextEncoder"), encoder).FromMaybe(false)) return false;

    v8::Local<v8::FunctionTemplate> decoder_template = v8::FunctionTemplate::New(isolate, TextDecoderConstructor);
    decoder_template->SetClassName(V8String(isolate, "TextDecoder"));
    decoder_template->InstanceTemplate()->SetInternalFieldCount(0);
    decoder_template->PrototypeTemplate()->Set(isolate, "decode", v8::FunctionTemplate::New(isolate, TextDecoderDecodeCallback));
    v8::Local<v8::Function> decoder;
    if (!decoder_template->GetFunction(context).ToLocal(&decoder)) return false;
    return context->Global()->Set(context, V8String(isolate, "TextDecoder"), decoder).FromMaybe(false);
}
```

- [ ] **Step 5: Install text globals from host setup**

In `src/v8/kode_host.cc`, call after `InstallKodeTextApi`:

```cpp
    if (!InstallTextEncodingGlobals(isolate, context)) return false;
```

- [ ] **Step 6: Verify Task 3 passes**

Run: `make build && make test-structured-runtime`

Expected: PASS.

- [ ] **Step 7: Commit Task 3**

```bash
git add Makefile src/v8/builtins/encoding.cc src/v8/kode_host.cc tests/text_encoder_decoder_basic.js tests/text_decoder_unsupported_label.js
git commit -m "feat: add text encoder decoder wrappers"
```

### Task 4: Documentation And Full Verification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Document all encoding surfaces**

Add this README example section after the crypto hashing example:

```md
### Text Encoding

```js
const encoding = require("kode:encoding")

const bytes = encoding.encodeUtf8("hello")
console.log(bytes instanceof Uint8Array)
console.log(encoding.decodeUtf8(bytes))
```

`Kode.text.encode` / `Kode.text.decode` provide the same UTF-8 behavior as global runtime helpers, while `TextEncoder` and `TextDecoder` provide small Web-style wrappers. Kode supports UTF-8 only in this slice and does not expose Node `Buffer`.
```

Also add `kode:encoding`, `Kode.text`, `TextEncoder`, and `TextDecoder` to the runtime API lists and architecture tree.

- [ ] **Step 2: Run full verification**

Run: `make build && make test-v8-microtask && make test-structured-runtime && make test-http && make test-concurrency`

Expected: PASS.

- [ ] **Step 3: Inspect diff**

Run: `git status --short && git diff --stat`

Expected: only encoding API and README changes are uncommitted.

- [ ] **Step 4: Commit docs**

```bash
git add -f README.md
git commit -m "docs: document encoding apis"
```
