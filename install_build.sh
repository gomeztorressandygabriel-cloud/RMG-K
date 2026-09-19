#!/bin/bash
set -e
cd /c/RMG-K-src
cmake --install Build/Release --strip --prefix="$(pwd)"
cmake --build Build/Release --target=bundle_dependencies
