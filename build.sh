#!/usr/bin/env bash
# Builds the .geode file (needs Geode CLI + SDK installed: https://docs.geode-sdk.org/getting-started/)
set -e
geode sdk install-binaries || true
geode build
echo "Done. Your .geode file is in ./build/"
