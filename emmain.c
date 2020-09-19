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

    vm_add_cmdline(&myparam, "console=hvc0 root=/dev/root ro");

    printf("Configure...\n");
    myvm = virt_machine_init(&myparam);
    printf("Run...\n");
    virt_machine_run(myvm);
    printf("Leave...\n");
}
