CXX = ccache g++
OUTPUT_FILE=bin/kode

define INCLUDE
	v8/include/
endef

define INCLUDEUV
	libuv/include/
endef

define APP
	app/index.cc app/fs.cc app/modern_fs.cc app/parser.cc
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
  uv-timers\
  v8-print-hello

# Build our Kode runtime (learning version with libuv only)
build:
	mkdir -p bin
	$(CXX) $$APP -I $$INCLUDEUV -std=c++20 -pthread -o $$OUTPUT_FILE libuv/libuv.a -Wl,--no-as-needed -ldl

# Future: Build with V8 when we fix the compatibility issues
build-v8:
	mkdir -p bin
	$(CXX) $$APP -I $$INCLUDE -I $$INCLUDEUV  -std=c++20 -pthread -o $$OUTPUT_FILE-v8 -DV8_COMPRESS_POINTERS -DV8_ENABLE_SANDBOX $$OBJ -Wl,--no-as-needed -ldl

# make uv-threads (or any from examples)
$(examples): % : examples/%.cpp
	mkdir -p bin
	$(CXX) -I $$INCLUDE -I $$INCLUDEUV  -std=c++17 -pthread -o bin/$@  $< -DV8_COMPRESS_POINTERS $$OBJ -Wl,--no-as-needed -ldl
	./bin/$@
clean:
	rm -rf bin