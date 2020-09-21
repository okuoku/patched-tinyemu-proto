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

/* fsopen_file, fsopen_dir */
typedef struct {
    FSDevice* fs;
    uint32_t type;
    uint32_t version;
    uint64_t path;
    FSOpenCompletionFunc* cb;
    void* opaque;
}fsopen_ticket;
static void
myfs_open_cb(int res, void* ctx){
    FSQID qid;
    fsopen_ticket* ticket = (fsopen_ticket*)ctx;
    fsopen_ticket tik = *ticket;
    free(ticket);
    qid.type = tik.type;
    qid.version = tik.version;
    qid.path = tik.path;
    printf("OpenCB: %d %d %lld\n",tik.type,tik.version,tik.path);
    tik.cb(tik.fs, &qid, res, tik.opaque);
}
static int (*fsopen_file)(uint32_t ctx, uint32_t loc, uint32_t tik, 
                          uint32_t followlink);
static int (*fsopen_dir)(uint32_t ctx, uint32_t loc, uint32_t followlink);
static int
myfs_open(FSDevice *fs, FSQID *qid, FSFile *f, uint32_t flags,
                   FSOpenCompletionFunc *cb, void *opaque){
    int r;
    fsopen_ticket* ticket;
    const uint32_t followlink = (flags & P9_O_NOFOLLOW) ? 0 : 1;
    /* Since we're ROFS, QID is constant */
    qid->type = f->type;
    qid->version = f->version;
    qid->path = f->path;
    if(flags & P9_O_DIRECTORY){
        /* Directory open is synchronous */
        r = fsopen_dir((uint32_t)(uintptr_t)fs, f->location, followlink);
        return r;
    }else{
        ticket = malloc(sizeof(fsopen_ticket));
        ticket->fs = fs;
        ticket->type = f->type;
        ticket->version = f->version;
        ticket->path = f->path;
        ticket->cb = cb;
        ticket->opaque = opaque;
        r = fsopen_file((uint32_t)(uintptr_t)fs, f->location,
                        (uint32_t)(uintptr_t)ticket, followlink);
        if(r<0){
            free(ticket);
        }
        return r;
    }
}

/* fsdelete */
static void (*fsdelete)(uint32_t ctx, uint32_t baseloc);
static void
myfs_delete(FSDevice* s, FSFile* f){
    fsdelete((uint32_t)(uintptr_t)s, f->location);
    free(f);
}

static void (*fsclose)(uint32_t ctx, uint32_t loc);
static void
myfs_close(FSDevice *fs, FSFile *f){
    fsclose((uint32_t)(uintptr_t)fs, f->location);
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

    int r,i;
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

    if(r > 0){
        for(i = 0;i!=r;i++){
            printf("Walk Q [%s] =>  %x:%d:%lld\n", names[i], types[i], versions[i], paths[i]);
            qids[i].type = types[i];
            qids[i].version = versions[i];
            qids[i].path = paths[i];
        }
        ident = malloc(sizeof(FSFile));
        ident->location = loc;
        ident->type = types[r-1];
        ident->version = versions[r-1];
        ident->path = paths[r-1];

        *pf = ident;
    }else if(r == 0){
        ident = malloc(sizeof(FSFile));
        ident->location = loc;
        ident->type = f->type;
        ident->version = f->version;
        ident->path = f->path;
        printf("IdentZero: %d %d %lld\n",f->type,f->version,f->path);

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
        printf("Stat %d => [%x:%d:%lld]\n", 
               f->location, f->type, f->version, f->path);

        st->st_mode = mode;
        /* FIXME: Linux enconding */
        switch(f->type){
            case 0x80: /* DIR */
                st->st_mode |= 0x4000;
                break;
            case 0x2: /* SIMLINK */
                st->st_mode |= 0xa000;
                break;
            default: /* REG */
                st->st_mode |= 0x8000;
                break;
        }
        st->st_uid = uid;
        st->st_gid = gid;
        st->st_nlink = 1;
        st->st_rdev = 0;
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

static int (*fsreadlink)(uint32_t ctx, uint32_t loc,
                         uint32_t out_addr_buf, uint32_t outlen);
static int
myfs_readlink(FSDevice *fs, char *buf, int buf_size, FSFile *f){
    int r;
    r = fsreadlink((uint32_t)(uintptr_t)fs,
                   f->location,
                   (uint32_t)(uintptr_t)buf, buf_size);
    printf("Readlink %d: [%s]\n", f->location, buf);
    return r;
}

/* Read */
static int (*fsread)(uint32_t ctx, uint32_t loc, 
                     uint32_t offs, /* FIXME: 32bit offset */
                     uint32_t out_addr_buf, uint32_t count);
static int
myfs_read(FSDevice *fs, FSFile *f, uint64_t offset,
            uint8_t *buf, int count){
    int r;
    r = fsread((uint32_t)(uintptr_t)fs,
               f->location, offset,
               (uint32_t)(uintptr_t)buf, count);
    return r;
}

static int
myfs_readdir(FSDevice *fs, FSFile *f, uint64_t offset,
                      uint8_t *buf, int count){
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
                    fsdelete = (void*)param1;
                    break;
                case 203:
                    fsstat = (void*)param1;
                    break;
                case 204:
                    fsreadlink = (void*)param1;
                    break;
                case 205:
                    fsopen_file = (void*)param1;
                    break;
                case 206:
                    fsopen_dir = (void*)param1;
                    break;
                case 207:
                    fsclose = (void*)param1;
                    break;
                case 208:
                    fsread = (void*)param1;
                    break;

                default:
                    fprintf(stderr, "Unknown fsop");
                    abort();
                    break;
            }
            break;
        case 200: /* OPEN_CB */
            myfs_open_cb(param0, (void*)param1);
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
