#!/bin/bash
set -euo pipefail

sudo apt install \
    git \
    build-essential \
    g++ \
    make \
    clang-format \
    libsdl2-dev \
    libsdl2-image-dev \
    libsdl2-ttf-dev \
    libsdl2-net-dev \
    libsdl2-mixer-dev \
    libsdl2-gfx-dev
