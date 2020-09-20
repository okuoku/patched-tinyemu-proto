const fs = require("fs");
const emu = require("./emu_tinyemu.js");

// Blobs
const kernel = fs.readFileSync("kernel");
const bios = fs.readFileSync("bbl32.bin");


function addfsops(module){
    // FIXME: Why not the_emu._ememu_configure ??
    const ememu_configure =
        module.cwrap("ememu_configure", "number", ["number", "number"]);

    function fill_qid(addr_type, addr_version, addr_path,
                      type, version, path){
        module.setValue(addr_type, type, "i32"); // our own protocol
        module.setValue(addr_version, version, "i32");
        module.setValue(addr_path, path, "i64");
    }
    function readpath(addr){
        return module.UTF8ToString(addr, 4096);
    }

    function fsattach(ctx, fsfile, uid,
                      str_uname, str_aname,
                      addr_qid_type, addr_qid_version, addr_qid_path){
        const uname = readpath(str_uname);
        const aname = readpath(str_aname);
        console.log("Attach",uid,uname,aname);
        return 5;
    }

    ememu_configure(102, 200, module.addFunction(fsattach, "iiiiiiiii"));

}

async function start(){
    const the_emu = await emu();

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
    addfsops(the_emu);

    ememu_start();
}

start();
