#!/bin/sh
# build + run the host harness inside a stock g++ container
cd "$(dirname "$0")"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(cd .. && pwd -W 2>/dev/null || pwd):/w" -w /w/host \
  mcr.microsoft.com/devcontainers/python:1-3.13-bookworm \
  sh -c "g++ -O2 -std=c++17 -Wall -I../src sim.cpp -o sim && ./sim"
