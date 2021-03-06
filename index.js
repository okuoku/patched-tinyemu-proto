const fs = require("fs");
const emu = require("./emu_tinyemu.js");

// root
const root = "c:/cygwin64/home/oku/repos/riscv32-rootfs-proto";

// Blobs
const kernel = fs.readFileSync("kernel");
const bios = fs.readFileSync("bbl32.bin");

function make_filetree(module, root, opencb){
    const rootdir = {elms: [], 
        entries: [],
        container: false, pathid: 999999,
    version: 999};
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
        if(! openfiles[loc]){
            console.log("Invalid deletion",loc,openfiles);
        }
        openfiles[loc] = false;
    }
    function size_dirent(name){
        const len = module.lengthBytesUTF8(name);
        return len + 13 /* QID */ + 8 /* offs */ + 1 /* dtype */ + 2 /* len */;
    }
    function fill_dirent(addr, type, version, path, next, dtype, name){
        const len = module.lengthBytesUTF8(name);
        //console.log("fill_dirent", addr, type, version, path, next, dtype, name);

        module.setValue(addr + 0, type, "i8");
        module.setValue(addr + 1, version & 0xff, "i8");
        module.setValue(addr + 2, (version>>8) & 0xff, "i8");
        module.setValue(addr + 3, (version>>16) & 0xff, "i8");
        module.setValue(addr + 4, (version>>24) & 0xff, "i8");
        module.setValue(addr + 5, path & 0xff, "i8");
        module.setValue(addr + 6, (path>>8) & 0xff, "i8");
        module.setValue(addr + 7, (path>>16) & 0xff, "i8");
        module.setValue(addr + 8, (path>>24) & 0xff, "i8");
        module.setValue(addr + 9, (path>>32) & 0xff, "i8");
        module.setValue(addr + 10, (path>>40) & 0xff, "i8");
        module.setValue(addr + 11, (path>>48) & 0xff, "i8");
        module.setValue(addr + 12, 0, "i8");
        module.setValue(addr + 13, next & 0xff, "i8");
        module.setValue(addr + 14, (next>>8) & 0xff, "i8");
        module.setValue(addr + 15, (next>>16) & 0xff, "i8");
        module.setValue(addr + 16, (next>>24) & 0xff, "i8");
        module.setValue(addr + 17, 0, "i8");
        module.setValue(addr + 18, 0, "i8");
        module.setValue(addr + 19, 0, "i8");
        module.setValue(addr + 20, 0, "i8");
        module.setValue(addr + 21, dtype, "i8");
        module.setValue(addr + 22, len & 0xff, "i8");
        module.setValue(addr + 23, (len >> 8) & 0xff, "i8");
        module.writeStringToMemory(name, addr + 24, true);
    }
    function fill_qid(addr_type, addr_version, addr_path,
                      type, version, path){
        module.setValue(addr_type, type, "i32"); // our own protocol
        module.setValue(addr_version, version, "i32");
        module.setValue(addr_path, path, "i64");
    }
    function follow_itr(cur, elms){
        if(elms.length > 1){
            const a = elms.shift();
            if(a == ".."){
                return follow_itr(cur.container, elms);
            }else{
                const next = cur.elms[a];
                if(next){
                    return follow_itr(next, elms);
                }else{
                    return false;
                }
            }
        }else{
            return cur;
        }
    }
    function follow_link(obj){
        if(obj.tgt[0] == "/"){
            return links[obj.tgt];
        }else{
            return follow_itr(obj, obj.tgt.split("/"));
        }
    }
    function resolve_link(follow, obj){
        if(! obj){
            return false;
        }else if(follow){
            if(obj.t == "s"){
                const resolved = resolve_link(follow, follow_link(obj));
                console.log("Symlink resolve",obj.n,resolved.n);
                return resolved;
            }else{
                return obj;
            }
        }else{
            return obj;
        }
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
        fileids[i].elms = []; // FIXME: Should be a dict
        fileids[i].entries = [];
    }

    links["/"] = rootdir;
    rootdir.container = rootdir;

    fileids.forEach(e => {
        links[e.n] = e;
    });

    // FIXME: Fixup symlinks (needs '/' denotes FS root )
    fileids.forEach(e => {
        if(e.t == "s" && links[e.tgt]){
            e.tgt = "/" + e.tgt;
        }
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
                cur.entries.push(p);
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
            if(n == 0){
                // Special case, walk to "."
                const output = location_add(cur);
                //console.log("walkzero", output);
                module.setValue(out_loc, output, "i32");
                return 0;
            }
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
                    break;
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
                //console.log("walk", loc);
                module.setValue(out_loc, loc, "i32");
                return n;
            }else{
                return -5;
            }
        },
        stat: function(ctx, loc, out_mode, out_uid, out_gid,
                       out_size, out_mtime, out_mtime_nsec){
            //console.log("Stat", loc);
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
                        size = me.entries.length;
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
                //console.log("Deleteloc", loc);
                location_del(loc);
            }
        },
        open_dir: function(ctx, loc, follow){
            const me = resolve_link(follow, openfiles[loc]);
            if(me.file.t == "d"){
                me.mode = "directory";
                me.state = 0;
                //console.log("Opendir", me);
                return 0;
            }else{
                return -5;
            }
        },
        open_file: function(ctx, loc, tik, follow){
            const me = resolve_link(follow, openfiles[loc]);
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
                return 1; /* Queued */
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
        },
        read: function(ctx, loc, offs, out, outlen){
            const me = openfiles[loc];
            if(me.file.t == "f" || me.file.t == "x"){
                let r = fs.readSync(me.state, module.HEAPU8, out, outlen, offs);
                return r;
            }else{
                return -5;
            }
        },
        readdir: function(ctx, loc, offs, out, outlen){
            const me = openfiles[loc];
            let elmoffs = offs - 2;
            let cur = 0;
            if(me.file.t != "d"){
                // FIXME: Check for openmode instead
                return -5;
            }
            for(;;){
                let name = false;
                let ent = false;
                if(elmoffs >= me.file.entries.length){
                    //console.log("Out", elmoffs, me.file.entries.length, me);
                    break;
                }

                if(elmoffs < 0){ /* . and .. */
                    const nam = (elmoffs == -2) ? "." : "..";
                    const p = (elmoffs == -2) ? me.file.container : me.file;
                    ent = {t: "d", n: nam,
                        version: p.version,
                        pathid: p.pathid,
                    };
                    name = nam;
                }else{
                    name = me.file.entries[elmoffs];
                    const baseent = me.file.elms[name];
                    const p = me.file.container;
                    ent = {
                        t: baseent.t,
                        n: name,
                        version: baseent.version,
                        pathid: baseent.pathid
                    };
                    //console.log("Ent", ent);
                    //ent.t = "d";
                    //ent.n = name;
                    //ent.version = p.version;
                    //ent.pathid = p.pathid;
                }

                /* Fill a entry */
                const entlen = size_dirent(ent.n);
                if(cur + entlen > outlen){
                    break;
                }
                let type = 0;
                let dtype = 0;
                if(ent.t == "s"){
                    type = 0x2;
                    dtype = 10; /* Linux dtype(LNK) */
                }else if(ent.t == "d"){
                    type = 0x80;
                    dtype = 4; /* Linux dtype(DIR) */
                }else{
                    dtype = 8; /* Linux dtype(REG) */
                }

                elmoffs++;
                //console.log(outlen, cur, name);
                fill_dirent(out + cur, type, ent.version, ent.pathid,
                            elmoffs + 2, dtype, name);

                cur += entlen;
                break;
            }
            return cur;
        },
        close: function(ctx, loc){
            const me = openfiles[loc];
            if(me){
                console.log("Closing", me);
                if(me.mode == "file_opened"){
                    fs.close(me.state);
                }
                me.mode = false;
                me.state = false;
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
    ememu_configure(102, 205, module.addFunction(treeops.open_file, "iiiii"));
    ememu_configure(102, 206, module.addFunction(treeops.open_dir, "iiii"));
    ememu_configure(102, 207, module.addFunction(treeops.close, "vii"));
    ememu_configure(102, 208, module.addFunction(treeops.read, "iiiiii"));
    ememu_configure(102, 209, module.addFunction(treeops.readdir, "iiiiii"));
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
