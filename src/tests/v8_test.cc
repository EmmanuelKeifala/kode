// Simple V8 test to verify V8 initialization works
#include <v8.h>
#include <libplatform/libplatform.h>
#include <iostream>
#include <memory>

int main(int argc, char* argv[]) {
    std::cout << "Testing V8 initialization..." << std::endl;
    
    // Initialize V8 - pass path to directory containing icudtl.dat
    std::cout << "1. InitializeICUDefaultLocation..." << std::endl;
    v8::V8::InitializeICUDefaultLocation("./bin");
    
    std::cout << "2. Creating platform..." << std::endl;
    std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
    
    std::cout << "3. InitializePlatform..." << std::endl;
    v8::V8::InitializePlatform(platform.get());
    
    std::cout << "4. Initialize..." << std::endl;
    v8::V8::Initialize();
    
    std::cout << "5. Creating isolate..." << std::endl;
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    v8::Isolate* isolate = v8::Isolate::New(create_params);
    
    if (!isolate) {
        std::cerr << "Failed to create isolate!" << std::endl;
        return 1;
    }
    
    std::cout << "6. Running JavaScript..." << std::endl;
    {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = v8::Context::New(isolate);
        v8::Context::Scope context_scope(context);
        
        v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, "'Hello from V8!'").ToLocalChecked();
        v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
        v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
        
        v8::String::Utf8Value utf8(isolate, result);
        std::cout << "Result: " << *utf8 << std::endl;
    }
    
    std::cout << "7. Cleanup..." << std::endl;
    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
    delete create_params.array_buffer_allocator;
    
    std::cout << "V8 test passed!" << std::endl;
    return 0;
}
