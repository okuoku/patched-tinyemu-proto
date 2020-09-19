const fs = require("fs");
const emu = require("./emu_tinyemu.js");

// Blobs
const kernel = fs.readFileSync("kernel");
const bios = fs.readFileSync("bbl32.bin");

async function start(){
    the_emu = await emu();

    // FIXME: Why not the_emu._ememu_configure ??
    const ememu_configure =
        the_emu.cwrap("ememu_configure", "number", ["number", "number"]);
    const ememu_start =
        the_emu.cwrap("ememu_start");

    const bios_len = bios.length;
    const kernel_len = kernel.length;

    const bios_addr = ememu_configure(1, bios_len, 0);
    const kernel_addr = ememu_configure(1, kernel_len, 0);

    the_emu.HEAPU8.set(bios, bios_addr);
    the_emu.HEAPU8.set(kernel, kernel_addr);

    ememu_configure(100, bios_addr, bios_len);
    ememu_configure(101, kernel_addr, kernel_len);

    ememu_start();
}

start();
