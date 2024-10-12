#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void copyfile(char *name, FILE *fout, int loc);

void copyfile(char *name, FILE *fout, int loc) {
    FILE* fin=fopen(name,"rb");
    if(fin==0) {
       perror(0);
       exit(1);
    }
    fseek(fout,loc,SEEK_SET);
    while(!feof(fin)) {
        int buf[256];
        int n=fread(buf,4,256,fin);
        fwrite(buf, 4, n, fout);
    }
    fclose(fin);
}


int main(int argc, char*argv[]) {
    int bootloader_pos=0x1000;
    int partitions_pos=0x8000;
    int firmware_pos=0x10000;
    int flash_size=4*1024*1024;
    char *machine=(char *)"esp32";
    if(argc<2) {
        printf("emulate firmware [build_dir] [s3]\n");
        exit(1);
    }
    if(argc>2 && !strcmp(argv[argc-1],"s3")) {
        bootloader_pos=0;
        machine=(char *)"esp32s3";
        flash_size=16*1024*1024;
    }
    char *firmware_name=argv[1];
    char build_dir[201];

    if(argc>2 && strcmp(argv[2],"s3")) {
        strncpy(build_dir,argv[2],200);
    } else {
       strncpy(build_dir,firmware_name,200);
       int l=strlen(build_dir);
       while(l>0 && build_dir[l]!='/' && build_dir[l]!='\\') l--;
       build_dir[l]=0;
    }

    char bootloader_name[256];
    char partitions_name[256];
    char cmd[512];
    snprintf(bootloader_name,256,"%s/bootloader.bin",build_dir);
    snprintf(partitions_name,256,"%s/partitions.bin",build_dir);
    char package_path[256];
    strncpy(package_path,argv[0],255);
    int l=strlen(package_path);
    while(l>0 && package_path[l]!='/' && package_path[l]!='\\') l--;
    package_path[l]=0;
#ifdef __APPLE__
    snprintf(cmd,512,"DYLD_LIBRARY_PATH=%s/xtensa-softmmu %s/xtensa-softmmu/qemu-system-xtensa -machine %s -drive file=esp32flash.bin,if=mtd,format=raw -display default,show-cursor=on -nic user,model=esp32_wifi,net=192.168.4.0/24,hostfwd=tcp::16555-192.168.4.1:80 -parallel none -monitor none"
            ,package_path,package_path,machine);
#else
    snprintf(cmd,512,"%s/xtensa-softmmu/qemu-system-xtensa -machine %s -drive file=esp32flash.bin,if=mtd,format=raw -display default,show-cursor=on -nic user,model=esp32_wifi,net=192.168.4.0/24,hostfwd=tcp::16555-192.168.4.1:80 -parallel none -monitor none"
            ,package_path,machine);
#endif

    FILE* fout=fopen("esp32flash.bin","r+b");
    if(fout==0) fout=fopen("esp32flash.bin","wb");
    copyfile(bootloader_name, fout, bootloader_pos);
    copyfile(partitions_name, fout, partitions_pos);
    copyfile(firmware_name, fout, firmware_pos);
    fseek(fout,flash_size-1,SEEK_SET);
    int x=0;
    fwrite(&x, 1, 1, fout);
    fclose(fout);
#ifdef __linux__
    unsetenv("GTK_PATH");
    unsetenv("GDK_PIXBUF_MODULEDIR");
    unsetenv("GDK_PIXBUF_MODULE_FILE");
#endif
    puts(cmd);
    int i=system(cmd);
    if(i<0) puts("Error Running Command");
    return 0;
}
