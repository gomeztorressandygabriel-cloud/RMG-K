#!/bin/bash
set -e
cd /c/RMG-K-src
cmake --build Build/Release --parallel $(nproc)
