KERNEL_DIR=$(pwd)
ZIP_DIR=$KERNEL_DIR/AKIW
IMAGE="${KERNEL_DIR}/out/arch/arm64/boot/Image.gz-dtb"
TANGGAL=$(date +"%Y%m%d-%H")
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
PATH="${HOME}/toolchain/clang/bin:${HOME}/toolchain/gcc/bin:${HOME}/toolchain/gcc32/bin:${PATH}"
export KBUILD_COMPILER_STRING="$(${HOME}/toolchain/clang/bin/clang --version | head -n 1 | perl -pe 's/\(http.*?\)//gs' | sed -e 's/  */ /g')"
export KBUILD_BUILD_USER=deadlylxrd
export KBUILD_BUILD_HOST=ubuntu

make O=out ARCH=arm64 sdm439-iw-perf_defconfig

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
