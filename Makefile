CXX = ccache g++
OUTPUT_FILE=bin/kode

define INCLUDE
	v8/include/
endef

define INCLUDEUV
	libuv/include/
endef

define APP
	src/main.cc src/core/runtime.cc src/filesystem/fs.cc src/filesystem/modern_fs.cc src/parser/parser.cc src/concurrency/task.cc src/http/http_server.cc
endef
 
define OBJ
	v8/libv8_monolith.a
	libuv/libuv.a
endef

export INCLUDE
export INCLUDEUV
export OUTPUT_FILE

export APP
export LIB
export OBJ
export APP

examples=\
  cpp-native-threads\
  uv-threads\
  v8-print-hello

build:
	mkdir -p bin
	ccache g++ $(APP) -I $(INCLUDEUV) -std=c++20 -pthread -o $(OUTPUT_FILE) libuv/libuv.a -Wl,--no-as-needed -ldl

test-concurrency:
	mkdir -p bin
	ccache g++ src/tests/concurrency_test.cc src/concurrency/task.cc -I $(INCLUDEUV) -std=c++20 -pthread -o bin/concurrency_test libuv/libuv.a -Wl,--no-as-needed -ldl

test-simple:
	mkdir -p bin
	ccache g++ src/tests/simple_concurrency_test.cc src/concurrency/task.cc -I $(INCLUDEUV) -std=c++20 -pthread -o bin/simple_test libuv/libuv.a -Wl,--no-as-needed -ldl

# Future: Build with V8 when we fix the compatibility issues
build-v8:
	mkdir -p bin
	$(CXX) $$APP -I $$INCLUDE -I $(INCLUDEUV)  -std=c++20 -pthread -o $$OUTPUT_FILE-v8 -DV8_COMPRESS_POINTERS -DV8_ENABLE_SANDBOX $$OBJ -Wl,--no-as-needed -ldl

$(examples): % : examples/%.cpp
	mkdir -p bin
	$(CXX) -I $$INCLUDE -I $(INCLUDEUV)  -std=c++17 -pthread -o bin/$@  $< -DV8_COMPRESS_POINTERS $$OBJ -Wl,--no-as-needed -ldl
	./bin/$@
clean:
	rm -rf bin