#include "encoding.h"

#include "../v8_helpers.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace kode { namespace v8embed {

namespace {

bool ReadBytesArg(v8::Isolate* isolate,
                  v8::Local<v8::Context> context,
                  const v8::FunctionCallbackInfo<v8::Value>& args,
                  int index,
                  const std::string& operation,
                  const uint8_t** data,
                  size_t* length) {
    if (args.Length() <= index) {
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EINVAL", "Missing byte input", operation, ""));
        return false;
    }

    v8::Local<v8::Value> value = args[index];
    if (value->IsArrayBufferView()) {
        v8::Local<v8::ArrayBufferView> view = value.As<v8::ArrayBufferView>();
        std::shared_ptr<v8::BackingStore> backing = view->Buffer()->GetBackingStore();
        *data = static_cast<const uint8_t*>(backing->Data()) + view->ByteOffset();
        *length = view->ByteLength();
        return true;
    }

    if (value->IsArrayBuffer()) {
        std::shared_ptr<v8::BackingStore> backing = value.As<v8::ArrayBuffer>()->GetBackingStore();
        *data = static_cast<const uint8_t*>(backing->Data());
        *length = backing->ByteLength();
        return true;
    }

    isolate->ThrowException(CreateKodeError(isolate, context,
        "EINVAL", "Expected Uint8Array, ArrayBuffer, or DataView", operation, ""));
    return false;
}

v8::Local<v8::Uint8Array> NewUint8Array(v8::Isolate* isolate, const std::string& bytes) {
    v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, bytes.size());
    if (!bytes.empty()) {
        std::shared_ptr<v8::BackingStore> backing = buffer->GetBackingStore();
        std::memcpy(backing->Data(), bytes.data(), bytes.size());
    }
    return v8::Uint8Array::New(buffer, 0, bytes.size());
}

void EncodeUtf8Callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    std::string input;
    if (!ReadStringArg(isolate, context, args, 0, "kode:encoding.encodeUtf8", &input)) return;

    args.GetReturnValue().Set(NewUint8Array(isolate, input));
}

void DecodeUtf8Callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    const uint8_t* data = nullptr;
    size_t length = 0;
    if (!ReadBytesArg(isolate, context, args, 0, "kode:encoding.decodeUtf8", &data, &length)) return;

    v8::Local<v8::String> decoded;
    if (!v8::String::NewFromUtf8(isolate,
                                 reinterpret_cast<const char*>(data),
                                 v8::NewStringType::kNormal,
                                 static_cast<int>(length)).ToLocal(&decoded)) {
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EINVAL", "Invalid UTF-8 input", "kode:encoding.decodeUtf8", ""));
        return;
    }
    args.GetReturnValue().Set(decoded);
}

} // namespace

v8::Local<v8::Object> CreateEncodingModule(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::Local<v8::Object> encoding = v8::Object::New(isolate);
    encoding->Set(context, V8String(isolate, "encodeUtf8"), v8::Function::New(context, EncodeUtf8Callback).ToLocalChecked()).FromMaybe(false);
    encoding->Set(context, V8String(isolate, "decodeUtf8"), v8::Function::New(context, DecodeUtf8Callback).ToLocalChecked()).FromMaybe(false);
    return encoding;
}

bool InstallTextEncodingGlobals(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    return true;
}

bool InstallKodeTextApi(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> kode) {
    return true;
}

} } // namespace kode::v8embed
