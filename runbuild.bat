emcc jsemu.c softfp.c virtio.c fs.c fs_net.c fs_wget.c fs_utils.c simplefb.c pci.c json.c block_net.c iomem.c cutils.c aes.c sha256.c riscv_cpu.c riscv_machine.c machine.c -O2 --llvm-opts 2 -Wall -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -MMD -fno-strict-aliasing -DCONFIG_FS_NET -O3 --memory-init-file 0 --closure 0 -s NO_EXIT_RUNTIME=1 -s NO_FILESYSTEM=1 -s "EXPORTED_FUNCTIONS=['_ememu_start', '_ememu_configure']" -s "EXTRA_EXPORTED_RUNTIME_METHODS=['cwrap','addFunction','UTF8ToString','stringToUTF8','lengthBytesUTF8','writeStringToMemory','setValue','getValue']" -s SINGLE_FILE=1 -s WASM=1 -s TOTAL_MEMORY=67108864 -s ALLOW_MEMORY_GROWTH=1 -s ALLOW_TABLE_GROWTH=1 -s MODULARIZE=1 -DMAX_XLEN=32 -DCONFIG_RISCV_MAX_XLEN=32 -s ASSERTIONS=1 -g4 --source-map-base http://localhost:6931/ emmain.c -o emu_tinyemu.js
