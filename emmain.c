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


/* LoadFile (from machine.c) */
static int load_file(uint8_t **pbuf, const char *filename)
{
    FILE *f;
    int size;
    uint8_t *buf;

    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(size);
    if (fread(buf, 1, size, f) != size) {
        fprintf(stderr, "%s: read error\n", filename);
        exit(1);
    }
    fclose(f);
    *pbuf = buf;
    return size;
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



int main(void){
    int len;
    uint8_t* buf;

    printf("Starting...\n");
    virt_machine_set_defaults(&myparam);
    myparam.ram_size = 256 * 1024 * 1024;
    myparam.machine_name = "riscv32";
    myparam.vmc = &riscv_machine_class;

    memset(&mychr, 0, sizeof(mychr));
    mychr.write_data = chr_write_data;
    mychr.read_data = chr_read_data;
    myparam.console = &mychr;

    len = load_file(&buf, "/bbl32.bin");
    myparam.files[VM_FILE_BIOS].buf = buf;
    myparam.files[VM_FILE_BIOS].len = len;
    printf("BIOS len = %d\n",len);

    len = load_file(&buf, "/kernel");
    myparam.files[VM_FILE_KERNEL].buf = buf;
    myparam.files[VM_FILE_KERNEL].len = len;
    printf("Kernel len = %d\n",len);

    vm_add_cmdline(&myparam, "console=hvc0 root=/dev/root ro");

    printf("Configure...\n");
    myvm = virt_machine_init(&myparam);
    printf("Run...\n");
    virt_machine_run(myvm);
    printf("Leave...\n");
    
    return 0;
}
