#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR=${1:-$PWD}
PORT=${2:-8080}

sudo apt update
sudo apt install -y build-essential cmake graphviz libboost-all-dev

cd "$PROJECT_DIR"
cmake -S . -B build
cmake --build build -j

echo "Compilacion lista. Ejecuta el backend con:"
echo "  ./build/MIA_P1 --server $PORT"
