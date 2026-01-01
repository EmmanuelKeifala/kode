#!/bin/bash
set -e

echo "[REBUILD V8] Building V8 for x86_64 architecture..."

# Ensure depot_tools is in PATH
export PATH="/home/emmanuel/opt/eode/depot_tools:$PATH"

# Create build directory
BUILD_DIR="$HOME/v8_build"
echo "[REBUILD V8] Build directory: $BUILD_DIR"

if [ -d "$BUILD_DIR/v8" ]; then
  echo "[REBUILD V8] V8 source already exists, updating..."
  cd "$BUILD_DIR/v8"
  git fetch
else
  echo "[REBUILD V8] Fetching V8 source code (this may take 10-15 minutes)..."
  mkdir -p "$BUILD_DIR"
  cd "$BUILD_DIR"
  fetch v8
  cd v8
fi

# Checkout a stable version
echo "[REBUILD V8] Checking out V8 version 12.4..."
git checkout branch-heads/12.4

# Sync dependencies
echo "[REBUILD V8] Syncing dependencies..."
gclient sync -D

# Configure build for x86_64 release build
echo "[REBUILD V8] Configuring build..."
gn gen out/x64.release --args='
  is_debug=false
  target_cpu="x64"
  v8_target_cpu="x64"
  v8_monolithic=true
  v8_use_external_startup_data=false
  is_component_build=false
  v8_enable_i18n_support=true
  treat_warnings_as_errors=false
  use_custom_libcxx=false
  v8_enable_pointer_compression=true
  v8_enable_sandbox=true
  use_sysroot=false
'

# Build V8
echo "[REBUILD V8] Building V8 (this will take 30-60 minutes)..."
ninja -C out/x64.release v8_monolith

# Verify the build
echo "[REBUILD V8] Verifying build architecture..."
file out/x64.release/obj/libv8_monolith.a
objdump -p out/x64.release/obj/libv8_monolith.a | grep "file format" | head -5

# Copy to kode project
echo "[REBUILD V8] Installing to project..."
KODE_DIR="/home/emmanuel/opt/kode"

# Backup old library
if [ -f "$KODE_DIR/v8/libv8_monolith.a" ]; then
  BACKUP_NAME="libv8_monolith.a.backup.$(date +%Y%m%d_%H%M%S)"
  mv "$KODE_DIR/v8/libv8_monolith.a" "$KODE_DIR/v8/$BACKUP_NAME"
  echo "[REBUILD V8] Backed up old library to v8/$BACKUP_NAME"
fi

# Copy new library
cp out/x64.release/obj/libv8_monolith.a "$KODE_DIR/v8/"
echo "[REBUILD V8] Installed libv8_monolith.a"

# Copy snapshot data
if [ -f out/x64.release/icudtl.dat ]; then
  cp out/x64.release/icudtl.dat "$KODE_DIR/v8/"
  echo "[REBUILD V8] Installed icudtl.dat"
fi

# Update headers if needed
echo "[REBUILD V8] Updating headers..."
rm -rf "$KODE_DIR/v8/include"
cp -r include "$KODE_DIR/v8/"
echo "[REBUILD V8] Installed headers"

echo ""
echo "========================================="
echo "[REBUILD V8] BUILD COMPLETE"
echo "========================================="
echo "Library: $KODE_DIR/v8/libv8_monolith.a"
echo "Size: $(du -h $KODE_DIR/v8/libv8_monolith.a | cut -f1)"
echo ""
echo "Test the build with: cd /home/emmanuel/opt/kode && make build"
echo ""
echo "Build directory: $BUILD_DIR"
echo "To save space, delete with: rm -rf $BUILD_DIR"
echo "========================================="
