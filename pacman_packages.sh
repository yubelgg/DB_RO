#!/bin/bash
# Arch Linux package installation script for dslabs-cpp
# Equivalent to apt_packages.sh for Debian/Ubuntu

set -e  # Exit on error

echo "Installing dependencies for dslabs-cpp on Arch Linux..."

# Update package database
sudo pacman -Sy

# Core build tools
echo "Installing core build tools..."
sudo pacman -S --needed --noconfirm base-devel cmake ninja meson autoconf automake pkgconf

# Compilers and toolchains
echo "Installing compilers..."
sudo pacman -S --needed --noconfirm gcc clang llvm

# C++ Libraries
echo "Installing C++ libraries..."
sudo pacman -S --needed --noconfirm boost boost-libs yaml-cpp jemalloc gperftools \
    gtest protobuf rocksdb gflags

# Python ecosystem
echo "Installing Python..."
sudo pacman -S --needed --noconfirm python python-pip cython python-docutils

# Create python symlink if it doesn't exist (python3 is default on Arch)
if [ ! -e /usr/bin/python ]; then
    echo "Creating /usr/bin/python symlink..."
    sudo ln -s /usr/bin/python3 /usr/bin/python
fi

# System libraries
echo "Installing system libraries..."
sudo pacman -S --needed --noconfirm openssl libffi systemd-libs

# NUMA and memory management
echo "Installing NUMA and memory libraries..."
sudo pacman -S --needed --noconfirm numactl libaio
# Note: pmdk not available in Arch official repos (available in AUR as 'pmdk' if needed)

# RDMA and high-performance networking
echo "Installing RDMA and networking libraries..."
sudo pacman -S --needed --noconfirm rdma-core libnl dpdk

# Development and debugging tools
echo "Installing development tools..."
sudo pacman -S --needed --noconfirm valgrind strace the_silver_searcher pandoc
# Note: libcgroup not available in Arch repos (cgroup functionality is built into systemd)

# Networking tools
echo "Installing networking utilities..."
sudo pacman -S --needed --noconfirm net-tools
# Note: ifmetric not available in Arch repos (use iproute2 for route metrics instead)

# Version control and deployment
echo "Installing version control tools..."
sudo pacman -S --needed --noconfirm git github-cli sshpass openssh

# Rust toolchain (skip if rustup is installed)
if command -v rustup &> /dev/null; then
    echo "rustup detected, skipping Arch rust package (conflict with rustup)"
else
    echo "Installing Rust..."
    sudo pacman -S --needed --noconfirm rust
fi

# RustyCpp borrow checker dependencies
echo "Installing RustyCpp dependencies..."
sudo pacman -S --needed --noconfirm llvm clang z3

echo ""
echo "✓ All packages installed successfully!"
echo ""
echo "Next steps:"
echo "1. Install Python dependencies: pip install -r requirements.txt"
echo "2. (Optional) Install Rust 1.80.0 via rustup: rustup install 1.80.0 && rustup default 1.80.0"
echo "3. Build the project: make clean && make labtest"
echo ""
echo "Note: Python 2 is deprecated and not installed. If needed, install from AUR: yay -S python2"
