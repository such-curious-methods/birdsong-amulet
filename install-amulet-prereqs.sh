#!/usr/bin/env bash
set -euo pipefail

if command -v dnf >/dev/null; then
    sudo dnf install -y \
        git \
        wget \
        flex \
        bison \
        gperf \
        gcc \
        gcc-c++ \
        libasan \
        libubsan \
        make \
        python3 \
        python3-pip \
        cmake \
        ninja-build \
        ccache \
        kicad \
        libffi-devel \
        openssl-devel \
        ncurses-devel \
        pkgconf-pkg-config \
        dfu-util \
        libusb1 \
        libusb1-devel \
        bluez \
        uv
elif command -v apt-get >/dev/null; then
    sudo apt-get update
    sudo apt-get install -y \
        build-essential \
        git \
        wget \
        flex \
        bison \
        gperf \
        python3 \
        python3-pip \
        python3-venv \
        cmake \
        ninja-build \
        ccache \
        kicad \
        libffi-dev \
        libssl-dev \
        libncurses-dev \
        pkg-config \
        dfu-util \
        libusb-1.0-0 \
        bluez
else
    printf 'Unsupported package manager; install the ESP-IDF host prerequisites manually.\n' >&2
    exit 1
fi

sudo usermod -aG dialout "$USER"

printf '\nAmulet host prerequisites installed.\n'
printf 'Log out and back in (or reboot) before flashing hardware so dialout membership applies.\n'
