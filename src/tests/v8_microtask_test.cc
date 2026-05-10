#include <v8.h>
#include <libplatform/libplatform.h>

#include <iostream>
#include <memory>

int main() {
    v8::V8::InitializeICUDefaultLocation("./bin");
    std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(platform.get());
    v8::V8::Initialize();

    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    v8::Isolate* isolate = v8::Isolate::New(create_params);
    isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);

    {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = v8::Context::New(isolate);
        v8::Context::Scope context_scope(context);

        v8::Local<v8::String> source = v8::String::NewFromUtf8(
            isolate,
            "let done = false; Promise.resolve().then(() => { done = true; }); done",
            v8::NewStringType::kNormal).ToLocalChecked();
        v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
        v8::Local<v8::Value> before = script->Run(context).ToLocalChecked();
        isolate->PerformMicrotaskCheckpoint();

        v8::Local<v8::String> check_source = v8::String::NewFromUtf8(
            isolate, "done", v8::NewStringType::kNormal).ToLocalChecked();
        v8::Local<v8::Script> check_script = v8::Script::Compile(context, check_source).ToLocalChecked();
        v8::Local<v8::Value> after = check_script->Run(context).ToLocalChecked();

        std::cout << "before=" << before->BooleanValue(isolate) << " after=" << after->BooleanValue(isolate) << std::endl;
    }

    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
    delete create_params.array_buffer_allocator;

    return 0;
}
