#!/bin/bash

# Colors
green='\033[01;32m'
red='\033[01;31m'
blink_red='\033[05;31m'
restore='\033[0m'

# Create toolchain folder
mkdir -p ~/toolchain

echo -e "${green}"
echo "~~~~~~~~~~~~~~~"
echo "Creating folder:"
echo "~~~~~~~~~~~~~~~"
echo -e "${restore}"

echo -e "${green}"
echo "~~~~~~~~~~~~~~~~~~~~~"
echo "Cloning dependencies:"
echo "~~~~~~~~~~~~~~~~~~~~~"
echo -e "${restore}"

# Clone AnyKernel
git clone https://github.com/IAmDeadlylxrd/AnyKernel3 -b fluttershy_sdm439 AK
git clone https://github.com/IAmDeadlylxrd/AnyKernel3 -b fluttershy_sdm439_iw AKIW

# Clone proton-clang
git clone --depth=1 https://github.com/silont-project/silont-clang ~/toolchain/clang

# Clone GCC 64bit
git clone --depth=1 https://github.com/LineageOS/android_prebuilts_gcc_linux-x86_aarch64_aarch64-linux-android-4.9 ~/toolchain/gcc

# Clone GCC 32bit
git clone --depth=1 https://github.com/LineageOS/android_prebuilts_gcc_linux-x86_arm_arm-linux-androideabi-4.9 ~/toolchain/gcc32

echo -e "${green}"
echo "~~~~"
echo "Done:"
echo "~~~~"
echo -e "${restore}"

KERNEL_DIR=$(pwd)
ZIP_DIR=$KERNEL_DIR/AK
IMAGE="${KERNEL_DIR}/out/arch/arm64/boot/Image.gz-dtb"
TANGGAL=$(date +"%Y%m%d-%H")
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
PATH="${HOME}/toolchain/clang/bin:${HOME}/toolchain/gcc/bin:${HOME}/toolchain/gcc32/bin:${PATH}"
export KBUILD_COMPILER_STRING="$(${HOME}/toolchain/clang/bin/clang --version | head -n 1 | perl -pe 's/\(http.*?\)//gs' | sed -e 's/  */ /g')"
export KBUILD_BUILD_USER=deadlylxrd
export KBUILD_BUILD_HOST=ubuntu

make O=out ARCH=arm64 sdm439-perf_defconfig

# Build start time
DATE_START=$(date +"%s")

# Compile plox
compile() {
	   make -j$(nproc) O=out \
                    ARCH=arm64 \
                    CC=clang \
                    OBJCOPY=llvm-objcopy \
                    OBJDUMP=llvm-objdump \
		    CROSS_COMPILE=aarch64-linux-android- \
                    CROSS_COMPILE_ARM32=arm-linux-androideabi- \
                    CLANG_TRIPLE=aarch64-linux-gnu-
		    LD="ccache ld.lld" \
		    AR=llvm-ar \
                    NM=llvm-nm \
                    OBJCOPY=llvm-objcopy \
                    OBJDUMP=llvm-objdump \
                    STRIP=llvm-strip $1 $2 $3
}

compile

# AnyKernel packing
cd $ZIP_DIR
cp $IMAGE zImage
make normal &>/dev/null
cd ..

# Build end time
DATE_END=$(date +"%s")
DIFF=$(($DATE_END - $DATE_START))

echo -e "${green}"
echo "----------------------------------------------"
echo "Build Completed in: $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds."
echo "----------------------------------------------"
echo -e "${restore}"
