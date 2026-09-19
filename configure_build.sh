#!/bin/bash
set -e
cd /c/RMG-K-src
cmake -S . -B Build/Release \
  -DCMAKE_BUILD_TYPE=Release \
  -DNETPLAY=ON \
  -DVRU=ON \
  -DUSE_ANGRYLION=ON \
  -DUPDATER=OFF \
  -DPORTABLE_INSTALL=ON \
  -G "MSYS Makefiles"
