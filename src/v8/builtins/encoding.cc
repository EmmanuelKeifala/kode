#include "encoding.h"

#include "../v8_helpers.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace kode { namespace v8embed {

namespace {

bool ReadByteInput(v8::Isolate* isolate,
                   v8::Local<v8::Context> context,
                   v8::Local<v8::Value> value,
                   const std::string& operation,
                   std::vector<uint8_t>* bytes) {
    const uint8_t* data = nullptr;
    size_t length = 0;

    if (value->IsUint8Array() || value->IsDataView()) {
        v8::Local<v8::ArrayBufferView> view = value.As<v8::ArrayBufferView>();
        std::shared_ptr<v8::BackingStore> backing = view->Buffer()->GetBackingStore();
        data = static_cast<const uint8_t*>(backing->Data()) + view->ByteOffset();
        length = view->ByteLength();
    } else if (value->IsArrayBuffer()) {
        std::shared_ptr<v8::BackingStore> backing = value.As<v8::ArrayBuffer>()->GetBackingStore();
        data = static_cast<const uint8_t*>(backing->Data());
        length = backing->ByteLength();
    } else {
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EINVAL", "Expected Uint8Array, ArrayBuffer, or DataView", operation, ""));
        return false;
    }

    if (length == 0) {
        bytes->clear();
    } else {
        bytes->assign(data, data + length);
    }
    return true;
}

v8::Local<v8::Uint8Array> EncodeUtf8(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     const std::string& text) {
    v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, text.size());
    if (!text.empty()) {
        std::shared_ptr<v8::BackingStore> backing = buffer->GetBackingStore();
        std::memcpy(backing->Data(), text.data(), text.size());
    }
    return v8::Uint8Array::New(buffer, 0, text.size());
}

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

    if (args.Length() < 1) {
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EINVAL", "Missing byte input", operation, ""));
        return;
    }

    std::vector<uint8_t> bytes;
    if (!ReadByteInput(isolate, context, args[0], operation, &bytes)) return;

    v8::Local<v8::String> decoded;
    const char* data = bytes.empty() ? "" : reinterpret_cast<const char*>(bytes.data());
    if (!v8::String::NewFromUtf8(isolate,
                                 data,
                                 v8::NewStringType::kNormal,
                                 static_cast<int>(bytes.size())).ToLocal(&decoded)) {
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EINVAL", "Invalid UTF-8 input", operation, ""));
        return;
    }
    args.GetReturnValue().Set(decoded);
}

void EncodeUtf8Callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    EncodeWithOperation(args, "kode:encoding.encodeUtf8");
}

void DecodeUtf8Callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    DecodeWithOperation(args, "kode:encoding.decodeUtf8");
}

void KodeTextEncodeCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    EncodeWithOperation(args, "Kode.text.encode");
}

void KodeTextDecodeCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    DecodeWithOperation(args, "Kode.text.decode");
}

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

void TextEncoderEncodeCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    EncodeWithOperation(args, "TextEncoder.encode");
}

void TextDecoderDecodeCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    DecodeWithOperation(args, "TextDecoder.decode");
}

} // namespace

v8::Local<v8::Object> CreateEncodingModule(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::Local<v8::Object> encoding = v8::Object::New(isolate);
    encoding->Set(context, V8String(isolate, "encodeUtf8"), v8::Function::New(context, EncodeUtf8Callback).ToLocalChecked()).FromMaybe(false);
    encoding->Set(context, V8String(isolate, "decodeUtf8"), v8::Function::New(context, DecodeUtf8Callback).ToLocalChecked()).FromMaybe(false);
    return encoding;
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

bool InstallKodeTextApi(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> kode) {
    v8::Local<v8::Object> text = v8::Object::New(isolate);
    text->Set(context, V8String(isolate, "encode"), v8::Function::New(context, KodeTextEncodeCallback).ToLocalChecked()).FromMaybe(false);
    text->Set(context, V8String(isolate, "decode"), v8::Function::New(context, KodeTextDecodeCallback).ToLocalChecked()).FromMaybe(false);
    FreezeValue(context, text);
    return kode->Set(context, V8String(isolate, "text"), text).FromMaybe(false);
}

} } // namespace kode::v8embed
