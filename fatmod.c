#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <linux/msdos_fs.h>


#define FALSE 0
#define TRUE 1

#define SECTORSIZE 512   // bytes
#define CLUSTERSIZE  1024  // bytes

// This is project/program to access and modify a FAT32 disk
// image(FAT32 volume). The disk image is stored as a regular Linux file, which
// will simulate a disk formatted with the FAT32 file system. This program opens
// the Linux file using the standard open() system call and access it directly in raw
// mode, utilizing the read() and write() system calls, without mounting the FAT32 file system.
// The program will be named fatmod. Through various options, it will interact with a file system image, enabling reading and writing of files.

// Function Prototypes
int readsector(int fd, unsigned char* buf, unsigned int snum);
int writesector(int fd, unsigned char* buf, unsigned int snum);


// Main function will read the given input once and given input will be executed
// Example invocations are:
/*
./ fatmod -h
./ fatmod disk1 -l
./ fatmod disk1 -c fileA.txt
./ fatmod disk1 -c fileB.bin
./ fatmod disk1 -w fileB.bin 0 3000 50
./ fatmod disk1 -r -b fileB.bin
./ fatmod disk1 -r -a fileC.txt  // assuming there is a non-empty ascii file fileC.txt
./ fatmod disk1 -r -a fileB.bin  // we can do this because it has printable chars
./ fatmod disk1 -d fileA.txt
./ fatmod disk1 -d fileB.bin
*/
int main(int argc, char* argv[]) {

    char diskname[128];
    int fd;
    unsigned char sector[SECTORSIZE];

    strcpy(diskname, argv[1]);

    fd = open(diskname, O_SYNC | O_RDWR);
    if (fd < 0) {
        printf("could not open disk image\n");
        exit(1);
    }

    // read the boot sector
    readsector(fd, sector, 0);
    printf("read sector 0\n");

    // ...
    close(fd);
}

int readsector(int fd, unsigned char* buf, unsigned int snum) {
    off_t offset;
    int n;
    offset = snum * SECTORSIZE;
    lseek(fd, offset, SEEK_SET);
    n = read(fd, buf, SECTORSIZE);
    if (n == SECTORSIZE)
        return (0);
    else
        return (-1);
}


int writesector(int fd, unsigned char* buf, unsigned int snum) {
    off_t offset;
    int n;
    offset = snum * SECTORSIZE;
    lseek(fd, offset, SEEK_SET);
    n = write(fd, buf, SECTORSIZE);
    fsync(fd);
    if (n == SECTORSIZE)
        return (0);
    else
        return (-1);
}
