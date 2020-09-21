const fs = require("fs");
const emu = require("./emu_tinyemu.js");

// root
const root = "c:/cygwin64/home/oku/repos/riscv32-rootfs-proto";

// Blobs
const kernel = fs.readFileSync("kernel");
const bios = fs.readFileSync("bbl32.bin");

function make_filetree(module, root, opencb){
    const rootdir = {elms: [], container: false};
    const openfiles = [];
    const fileids = JSON.parse(fs.readFileSync(root + "/_filelist.json", "utf8"));
    const links = {};

    function location_add(obj){ // => int
        let i = 0;
        for(i=0;i!=openfiles.length;i++){
            if(! openfiles[i]){
                break;
            }
        }
        if(i == openfiles.length){
            openfiles.push(false);
        }
        openfiles[i] = {file: obj, state: false, mode: false};

        return i;
    }

    function location_del(loc){
        openfiles[loc] = false;
    }
    function fill_qid(addr_type, addr_version, addr_path,
                      type, version, path){
        module.setValue(addr_type, type, "i32"); // our own protocol
        module.setValue(addr_version, version, "i32");
        module.setValue(addr_path, path, "i64");
    }
    function readptr(addr){
        return module.getValue(addr, "i32");
    }
    function readpath(addr){
        return module.UTF8ToString(addr, 4096);
    }

    for(let i=0; i!=fileids.length; i++){
        fileids[i].pathid = i;
        fileids[i].version = 1;
        fileids[i].refcount = 0;
        fileids[i].content = false;
        fileids[i].container = false;
        fileids[i].elms = [];
    }

    links["/"] = rootdir;

    fileids.forEach(e => {
        links[e.n] = e;
    });

    // Construct tree entries
    fileids.forEach(e => {
        const elms = e.n.split("/");
        let cur = rootdir;
        let curpath = "";
        elms.forEach(p => {
            curpath += p;
            if(! cur.elms[p]){
                cur.elms[p] = links[curpath];
                links[curpath].container = cur;
            }
            cur = links[curpath];
            curpath += "/";
        });
    });

    location_add(rootdir);

    return {
        walk: function(ctx, baseloc, n, names, out_loc,
                       out_types, out_versions, out_paths){
            let cur = openfiles[baseloc].file;
            let look = false;
            for(let i=0;i!=n;i++){
                const namep = readptr(i*4 + names);
                const typep = out_types + i*4;
                const versionp = out_versions + i*4;
                const pathp = out_paths + i*8;

                const name = readpath(namep);

                if(name == ".."){
                    look = cur.container;
                }else{
                    look = cur.elms[name];
                }

                //console.log("Walk", name, cur, look);

                if(! look){
                    return i;
                }else{
                    let type = 0;
                    switch(look.t){
                        case "d":
                            type = 0x80;
                            break;
                        case "s":
                            type = 0x2;
                            break;
                        case "f":
                        case "x":
                            type = 0;
                            break;
                        default:
                            break;
                    }
                    const path = look.pathid;
                    const version = look.version;
                    fill_qid(typep, versionp, pathp,
                             type, version, path);
                }
            }
            if(look){
                const loc = location_add(look);
                console.log("walk", loc);
                module.setValue(out_loc, loc, "i32");
                return n;
            }else{
                return -5;
            }
        },
        stat: function(ctx, loc, out_mode, out_uid, out_gid,
                       out_size, out_mtime, out_mtime_nsec){
            console.log("Stat", loc);
            const mtime_sec = 0;
            const mtime_nsec = 0;
            const mode = 0x1ff; /* Octal 0777 */
            const me = openfiles[loc].file;
            const uid = 0;
            const gid = 0;
            let size =0;
            if(me){
                switch(me.t){
                    case "s":
                        size = me.n.length;
                        break;
                    case "d":
                        size = me.elms.length;
                        break;
                        
                    default:
                        const mystat = me.n ? fs.statSync(root + "/" + me.n) : false;
                        size = mystat ? mystat.size : 0;

                        break;
                }
                module.setValue(out_mode, mode, "i32");
                module.setValue(out_uid, uid, "i32");
                module.setValue(out_gid, gid, "i32");
                module.setValue(out_size, size, "i64");
                module.setValue(out_mtime, mtime_sec, "i64");
                module.setValue(out_mtime_nsec, mtime_nsec, "i32");

                return 0;
            }else{
                return -5;
            }
        },
        deleteloc: function(ctx, loc){
            if(loc == 0){
                // Disallow deleting root location
                console.log("Ignore deleteloc 0");
            }else{
                console.log("Deleteloc", loc);
                location_del(loc);
            }
        },
        open_dir: function(ctx, loc){
            const me = openfiles[loc];
            if(me.file.t == "d"){
                me.mode = "directory";
                me.state = 0;
                return 0;
            }else{
                return -5;
            }
        },
        open_file: function(ctx, loc, tik){
            const me = openfiles[loc];
            if(me.file.t == "d"){
                console.log("Invalid open request", loc);
                return -5;
            }else{
                me.mode = "file_start";
                me.state = false;
                fs.open(root + "/" + me.file.n, "r",
                        (err, fd) => {
                            if(! err){
                                me.mode = "file_opened";
                                me.state = fd;
                                opencb(0, tik);
                            }else{
                                opencb(err, tik);
                            }
                        });
                return 0;
            }
        },
        readlink: function(ctx, loc, out, outlen){
            const me = openfiles[loc];
            if(me.file.t = "s"){
                //console.log("Readlink",loc,me.file);
                module.stringToUTF8(me.file.tgt, out, outlen);
                return 0;
            }else{
                console.log("Readlink (fail)",loc, me);
                return -5;
            }
        }
    };
}

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
        fill_qid(addr_qid_type, addr_qid_version, addr_qid_path,
                 0x80, 999, 99999);
        console.log("Attach",uid,uname,aname);
        return 0;
    }
    function opencb(err, ticket){
        ememu_configure(200, err, ticket);
    }
    const treeops = make_filetree(module, root, opencb);

    ememu_configure(102, 200, module.addFunction(fsattach, "iiiiiiiii"));
    ememu_configure(102, 201, module.addFunction(treeops.walk, "iiiiiiiii"));
    ememu_configure(102, 202, module.addFunction(treeops.deleteloc, "vii"));
    ememu_configure(102, 203, module.addFunction(treeops.stat, "iiiiiiiii"));
    ememu_configure(102, 204, module.addFunction(treeops.readlink, "iiiii"));
    ememu_configure(102, 205, module.addFunction(treeops.open_file, "iiii"));
    ememu_configure(102, 206, module.addFunction(treeops.open_dir, "iii"));
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
