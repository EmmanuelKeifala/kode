#include <v8.h>
#include <libplatform/libplatform.h>
#include <iostream>

int main() {
    // Initialize V8
    v8::V8::InitializeICUDefaultLocation("");
    v8::V8::InitializeExternalStartupData("");
    std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(platform.get());
    v8::V8::Initialize();

    std::cout << "V8 version: " << v8::V8::GetVersion() << std::endl;
    std::cout << "V8 initialized successfully!" << std::endl;

    // Clean up
    v8::V8::Dispose();
    
    return 0;
}
