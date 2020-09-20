#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "cutils.h"
#include "iomem.h"
#include "virtio.h"
#include "machine.h"

void virt_machine_run(VirtMachine* opaque);
VirtMachine* myvm;
VirtMachineParams myparam;

// library stubs
void 
console_write(void* ctx, const uint8_t* buf, int len){
}

void
console_get_size(int* pw, int* ph){
    *pw = 80;
    *ph = 80;
}

void
fb_refresh(void* ctx, void* data, 
           int x, int y, int w, int h, int stride){
}

void
net_recv_packet(void* dev,
                const uint8_t* buf, int len){
}

/* Chrdev */
CharacterDevice mychr;

static void
chr_write_data(void* bogus, const uint8_t *buf, int len){
    for(int i=0;i!=len;i++){
        //printf("CONS: %02x\n", buf[i]);
        fputc(buf[i], stdout);
    }
}

static int 
chr_read_data(void* bogus, uint8_t *buf, int len){
    return 0;
}

/* Stubs */

static void
myfs_end(FSDevice *s){
    /* UNUSED */
}

static void
myfs_delete(FSDevice* s, FSFile* f){
    /* FIXME: implement this */
}

static void
myfs_statfs(FSDevice* fs, FSStatFS* st){
    st->f_bsize = 1024;
    st->f_blocks = 999999;
    st->f_bfree = 999999;
    st->f_bavail = 999999;
    st->f_files = 999999;
    st->f_ffree = 999999;
}

/* System OPs */
struct FSFile {
    int location;
    uint32_t type;
    uint32_t version;
    uint64_t path;
};

/* fsattach: 200 */
typedef struct FSFile FSFile;
static int (*fsattach)(uint32_t ctx,
                       uint32_t fsfile, uint32_t uid, 
                       uint32_t str_uname, uint32_t str_aname,
                       uint32_t out_addr_qid_type /* 32bit */,
                       uint32_t out_addr_qid_version /* 32bit */,
                       uint32_t out_addr_qid_path /* 64bit */);


static int
myfs_attach(FSDevice *fs, FSFile **pf, FSQID *qid, uint32_t uid,
                     const char *uname, const char *aname){
    FSFile* ident;
    int r;
    uint32_t type;
    uint32_t version;
    uint64_t path;
    ident = malloc(sizeof(FSFile));
    ident->location = 0; /* ROOT */
    r = fsattach((uint32_t)(uintptr_t)fs,
                 (uint32_t)(uintptr_t)ident, uid,
                 (uint32_t)(uintptr_t)uname,
                 (uint32_t)(uintptr_t)aname,
                 (uint32_t)(uintptr_t)&type,
                 (uint32_t)(uintptr_t)&version,
                 (uint32_t)(uintptr_t)&path);
    if(r){
        free(ident);
        return r;
    }else{
        ident->type = type;
        ident->version = version;
        ident->path = path;
        *pf = ident;
        qid->type = type;
        qid->version = version;
        qid->path = path;
        return 0;
    }
}

static int
myfs_open(FSDevice *fs, FSQID *qid, FSFile *f, uint32_t flags,
                   FSOpenCompletionFunc *cb, void *opaque){
    return -P9_EIO;
}

static void
myfs_close(FSDevice *fs, FSFile *f){
}

/* Tree Query */

/* fswalk: 201 */
static int (*fswalk)(uint32_t ctx,
                     uint32_t baseloc,
                     uint32_t n,
                     uint32_t str_names,
                     uint32_t out_addr_loc,
                     uint32_t out_addr_types,
                     uint32_t out_addr_versions,
                     uint32_t out_addr_paths /* 64bits */);
static int
myfs_walk(FSDevice *fs, FSFile **pf, FSQID *qids,
                   FSFile *f, int n, char **names){

    int r;
    uint32_t loc;
    uint32_t* types = malloc(n * sizeof(uint32_t));
    uint32_t* versions = malloc(n * sizeof(uint32_t));
    uint64_t* paths = malloc(n * sizeof(uint64_t));
    FSFile* ident;

    r = fswalk((uint32_t)(uintptr_t)fs, f->location, n,
               (uint32_t)(uintptr_t)names,
               (uint32_t)(uintptr_t)&loc,
               (uint32_t)(uintptr_t)types,
               (uint32_t)(uintptr_t)versions,
               (uint32_t)(uintptr_t)paths);

    if(r >= 0){
        for(int i = 0;i!=r;i++){
            qids[i].type = types[i];
            qids[i].version = versions[i];
            qids[i].path = paths[i];
        }
        ident = malloc(sizeof(FSFile));
        ident->location = loc;

        *pf = ident;
    }

    free(types);
    free(versions);
    free(paths);

    return r;
}

static int (*fsstat)(uint32_t ctx, uint32_t loc,
                     uint32_t out_mode,
                     uint32_t out_uid, uint32_t out_gid,
                     uint32_t out_size, uint32_t out_mtime_sec,
                     uint32_t out_mtime_nsec);
static int
myfs_stat(FSDevice *fs, FSFile *f, FSStat *st){
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint64_t mtime_sec;
    uint32_t mtime_nsec;
    int r;
    r = fsstat((uint32_t)(uintptr_t)fs,
               f->location,
               (uint32_t)(uintptr_t)&mode,
               (uint32_t)(uintptr_t)&uid,
               (uint32_t)(uintptr_t)&gid,
               (uint32_t)(uintptr_t)&size,
               (uint32_t)(uintptr_t)&mtime_sec,
               (uint32_t)(uintptr_t)&mtime_nsec);
    if(! r){
        st->qid.type = f->type;
        st->qid.version = f->version;
        st->qid.path = f->path;

        st->st_mode = mode;
        st->st_uid = uid;
        st->st_gid = gid;
        st->st_nlink = 1;
        st->st_rdev = 99;
        st->st_size = size;
        st->st_blksize = size;
        st->st_blocks = 1;
        st->st_atime_sec = 0;
        st->st_atime_nsec = 0;
        st->st_mtime_sec = mtime_sec;
        st->st_mtime_nsec = mtime_nsec;
        st->st_ctime_sec = 0;
        st->st_ctime_nsec = 0;
        return 0;
    }else{
        return r;
    }
}

static int
myfs_readdir(FSDevice *fs, FSFile *f, uint64_t offset,
                      uint8_t *buf, int count){
    return -P9_EIO;
}

/* Read */

static int
myfs_read(FSDevice *fs, FSFile *f, uint64_t offset,
            uint8_t *buf, int count){
    return -P9_EIO;
}

static int
myfs_readlink(FSDevice *fs, char *buf, int buf_size, FSFile *f){
    return -P9_EIO;
}

/* Tree Write */

static int
myfs_mkdir(FSDevice *fs, FSQID *qid, FSFile *f,
                    const char *name, uint32_t mode, uint32_t gid){
    /* WRITE */
    return -P9_EIO;
}

static int
myfs_create(FSDevice *fs, FSQID *qid, FSFile *f, const char *name,
                     uint32_t flags, uint32_t mode, uint32_t gid){
    /* WRITE */
    return -P9_EIO;
}

static int
myfs_setattr(FSDevice *fs, FSFile *f, uint32_t mask,
                      uint32_t mode, uint32_t uid, uint32_t gid,
                      uint64_t size, uint64_t atime_sec, uint64_t atime_nsec,
                      uint64_t mtime_sec, uint64_t mtime_nsec){
    /* WRITE */
    return -P9_EIO;
}

static int
myfs_link(FSDevice *fs, FSFile *df, FSFile *f, const char *name){
    /* WRITE */
    return -P9_EIO;
}

static int
myfs_mknod(FSDevice *fs, FSQID *qid,
                    FSFile *f, const char *name, uint32_t mode, uint32_t major,
                    uint32_t minor, uint32_t gid){
    /* WRITE */
    return -P9_EIO;
}

static int
myfs_renameat(FSDevice *fs, FSFile *f, const char *name,
                       FSFile *new_f, const char *new_name){
    /* WRITE */
    return -P9_EIO;
}

static int
myfs_unlinkat(FSDevice *fs, FSFile *f, const char *name){
    /* WRITE */
    return -P9_EIO;
}

/* Write */
static int
myfs_write(FSDevice *fs, FSFile *f, uint64_t offset,
             const uint8_t *buf, int count){
    /* WRITE */
    return -P9_EIO;
}

static int
myfs_symlink(FSDevice *fs, FSQID *qid,
                      FSFile *f, const char *name, const char *symgt, uint32_t gid){
    /* WRITE */
    return -P9_EIO;
}

static int
myfs_lock(FSDevice *fs, FSFile *f, const FSLock *lock){
    /* FIXME: N/A */
    return 0;
}

static int
myfs_getlock(FSDevice *fs, FSFile *f, FSLock *lock){
    /* FIXME: N/A */
    return 0;
}

FSDevice myfs = {
    .fs_end = myfs_end,
    .fs_delete = myfs_delete,
    .fs_statfs = myfs_statfs,

    .fs_attach = myfs_attach,
    .fs_walk = myfs_walk,
    .fs_mkdir = myfs_mkdir,
    .fs_open = myfs_open,
    .fs_create = myfs_create,
    .fs_stat = myfs_stat,
    .fs_setattr = myfs_setattr,
    .fs_close = myfs_close,
    .fs_readdir = myfs_readdir,
    .fs_read = myfs_read,
    .fs_write = myfs_write,
    .fs_link = myfs_link,
    .fs_symlink = myfs_symlink,
    .fs_mknod = myfs_mknod,
    .fs_readlink = myfs_readlink,
    .fs_renameat = myfs_renameat,
    .fs_unlinkat = myfs_unlinkat,
    .fs_lock = myfs_lock,
    .fs_getlock = myfs_getlock
};

void* bios_ptr;
int bios_size;

void* kernel_ptr;
int kernel_size;

uintptr_t 
ememu_configure(int req, uintptr_t param0, uintptr_t param1){
    uintptr_t r = 0;
    switch(req){
        case 1: /* malloc */
            r = (uintptr_t)malloc(param0);
            break;
        case 100: /* SET_BIOS */
            bios_ptr = (void*)param0;
            bios_size = param1;
            break;
        case 101: /* SET_KERNEL */
            kernel_ptr = (void*)param0;
            kernel_size = param1;
            break;
        case 102: /* SET_FSOPS */
            switch(param0){
                case 200:
                    fsattach = (void*)param1;
                    break;
                case 201:
                    fswalk = (void*)param1;
                    break;
                case 202:
                    fsstat = (void*)param1;
                    break;
                default:
                    fprintf(stderr, "Unknown fsop");
                    abort();
                    break;
            }
            break;
        default:
            fprintf(stderr, "Invalid argument.\n");
            abort();
            break;
    }
    printf("CONFIG %d: %lx %lx => %lx\n", req, param0, param1, r);
    return r;
}



void 
ememu_start(void){
    printf("Starting...\n");
    virt_machine_set_defaults(&myparam);
    myparam.ram_size = 256 * 1024 * 1024;
    myparam.machine_name = "riscv32";
    myparam.vmc = &riscv_machine_class;

    memset(&mychr, 0, sizeof(mychr));
    mychr.write_data = chr_write_data;
    mychr.read_data = chr_read_data;
    myparam.console = &mychr;

    myparam.files[VM_FILE_BIOS].buf = bios_ptr;
    myparam.files[VM_FILE_BIOS].len = bios_size;

    myparam.files[VM_FILE_KERNEL].buf = kernel_ptr;
    myparam.files[VM_FILE_KERNEL].len = kernel_size;

    myparam.fs_count = 1;
    myparam.tab_fs[0].tag = "/dev/root";
    myparam.tab_fs[0].fs_dev = &myfs;

    vm_add_cmdline(&myparam, "console=hvc0 root=/dev/root rootfstype=9p ro");

    printf("Configure...\n");
    myvm = virt_machine_init(&myparam);
    printf("Run...\n");
    virt_machine_run(myvm);
    printf("Leave...\n");
}
