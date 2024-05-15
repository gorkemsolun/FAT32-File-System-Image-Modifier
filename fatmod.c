// Görkem Kadir Solun 10.05.2024
// 
// This is project/program to access and modify a FAT32 disk
// image(FAT32 volume). The disk image is stored as a regular Linux file, which
// will simulate a disk formatted with the FAT32 file system. This program opens
// the Linux file using the standard open() system call and access it directly in raw
// mode, utilizing the read() and write() system calls, without mounting the FAT32 file system.
// The program will be named fatmod. Through various options, it will interact with a file system image, enabling reading and writing of files.


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

// ___Definitions___

#define FAILURE -1
#define SUCCESS 0
#define INVALID_ARGUMENTS "Invalid arguments. Please enter -h for help\n"
#define FAT_TABLE_END_OF_FILE_VALUE 0x0FFFFFF8
#define FAT_TABLE_BAD_CLUSTER_VALUE 0x0FFFFFF7
#define FAT_TABLE_FREE_CLUSTER_VALUE 0x00000000
#define FAT_TABLE_RESERVED_CLUSTER_VALUE 0x0FFFFFF0
#define FAT_TABLE_LAST_CLUSTER_VALUE 0x0FFFFFFF
#define MAX_NUMBER_OF_CLUSTERS_FAT_TABLE 0x10000000 // 2^28, fat entry high 4 bits are reserved

// Options of the root directory function
#define FIND_GIVEN_ENTRY 0
#define LIST_DIRECTORIES 1
#define FIND_FREE_ENTRY 2

#define SECTORSIZE 512   // bytes
#define CLUSTERSIZE  1024  // bytes
#define FILENAME_SIZE 8 // bytes
#define FILE_EXTENSION_SIZE 3 // bytes
#define DOT_SIZE 1 // bytes
#define FAT_TABLE_ENTRY_SIZE 4 // bytes
#define TOTAL_FILENAME_SIZE FILE_EXTENSION_SIZE + FILENAME_SIZE // bytes    
#define FILE_DIRECTORY_ENTRY_SIZE 32 // bytes

#define N_RESERVED_SECTORS 32 // it is assumed that the reserved sectors are 32
#define N_ROOT_DIRECTORY_CLUSTERS 1 // it is assumed that the root directory is stored in one cluster
#define N_FAT_TABLES 1 // it is assumed that there is only one FAT table
#define ASSUMED_CLUSTER_SIZE 2 // it is assumed that the cluster size is 2 sectors   
#define ASSUMED_SEC_PER_CLUS CLUSTERSIZE/SECTORSIZE // it is assumed that the sectors per cluster is 2
#define ASSUMED_ROOT_DIRECTORY_CLUSTER 2 // it is assumed that the root directory is stored in the second cluster

// ___Function Prototypes___

// Read the root directory from the disk image which is stored in the second cluster
int read_root_directory(int fd, int is_list_directories);
// Read the file in binary or ASCII
void read_file(int fd, int is_binary);
// Create a file named with given input in the root directory
int create_file_entry(int fd);
// Delete the file named with given input in the root directory, and free the blocks allocated for it in the FAT
int delete_file(int fd);
// Write the bytes to the file
int write_bytes_to_file(int fd, int start_offset, int length, int data);
// print the help message about the usage of the program
void print_help_message();

int read_sector(int fd, unsigned char* buffer, unsigned int snum);
int read_cluster(int fd, unsigned char* buffer, unsigned int cluster_number);

int write_sector(int fd, unsigned char* buffer, unsigned int snum);
int write_cluster(int fd, unsigned char* buffer, unsigned int cluster_number);
int write_fat_table_entry(int fd, unsigned int cluster_number, unsigned int value);
int write_file_directory_entry(int fd, int directory_entry_index);

int get_next_FAT_table_entry(int fd, unsigned int cluster_number);

int char_overflow_check(int value);
int bytes_to_int(char* bytes, int length);
int unsigned_bytes_to_int(unsigned char* bytes, int length);
void int_to_bytes(int val, char* bytes);
void int_to_unsigned_bytes(int val, unsigned char* bytes);

int check_set_file_name(char* str);
int get_length_of_file_name(char* str);



// ___Global Variables___

int sector_size;
int reserved_sectors;
int total_sectors;

int root_directory_cluster_number; // in clusters
int root_directory_cluster_offset; // in sectors
int root_directory_max_content_size; // in directory entries

int sectors_per_cluster;
int usable_clusters_size;

int fat_size; // in sectors 
int usable_fat_table_size;
int number_of_fat_tables;
int fat_table_offset; // in sectors

unsigned char boot_sector_raw[SECTORSIZE];
// Used for reading the root directory, which is stored in the second cluster
unsigned char root_directory[CLUSTERSIZE];
unsigned char file_directory_entry_raw[FILE_DIRECTORY_ENTRY_SIZE];
// Used for reading the FAT table entry
unsigned char fat_table_entry[FAT_TABLE_ENTRY_SIZE];
// Used for reading the root directory, file name + dot + extension
char total_file_name[TOTAL_FILENAME_SIZE + DOT_SIZE];
// Given input file name
char input_file_name[TOTAL_FILENAME_SIZE];

struct fat_boot_sector* boot_sector;
struct msdos_dir_entry* file_directory_entry;



// ___Function Implementations___

// Main function will read the given input once and given input will be executed
// Example invocations are:
/*
./ fatmod -h
./ fatmod disk1 -l
./ fatmod disk1 -c fileA.txt
./ fatmod disk1 -w fileB.bin 0 3000 50
./ fatmod disk1 -r -b fileB.bin
./ fatmod disk1 -r -a fileC.txt  // assuming there is a non-empty ascii file fileC.txt
./ fatmod disk1 -d fileA.txt
*/
int main(int argc, char* argv[]) {
    // Check if the user has entered the correct number of arguments
    if (argc == 2) {
        if (strcmp(argv[1], "-h") == 0) {
            print_help_message();
            return 0;
        } else {
            printf("%s", INVALID_ARGUMENTS);
            return 0;
        }
    } else if (argc < 3) {
        printf("%s", INVALID_ARGUMENTS);
        return 0;
    }


    // Read the disk image name then open it
    char diskname[128];
    strcpy(diskname, argv[1]);
    int fd = open(diskname, O_SYNC | O_RDWR);
    if (fd < 0) {
        printf("Could not open disk image!\n");
        exit(1);
    }


    // Read the boot sector from the disk image 
    int n = read_sector(fd, boot_sector_raw, 0);
    if (n < 0) {
        printf("Could not read boot sector!\n");
        close(fd);
        exit(1);
    }

    // Cast the boot sector to a struct 
    boot_sector = (struct fat_boot_sector*) boot_sector_raw;
    reserved_sectors = boot_sector->reserved;
    if (reserved_sectors != N_RESERVED_SECTORS) {
        printf("WARNING: Reserved sectors is not %d!\n", N_RESERVED_SECTORS);
    }
    sectors_per_cluster = boot_sector->sec_per_clus;
    if (sectors_per_cluster != ASSUMED_SEC_PER_CLUS) {
        printf("WARNING: Sectors per cluster is not %d!\n", ASSUMED_SEC_PER_CLUS);
    }
    root_directory_cluster_number = boot_sector->fat32.root_cluster;
    if (root_directory_cluster_number != ASSUMED_ROOT_DIRECTORY_CLUSTER) {
        printf("WARNING: Root directory cluster number is not %d!\n", ASSUMED_ROOT_DIRECTORY_CLUSTER);
    }
    number_of_fat_tables = boot_sector->fats;
    if (number_of_fat_tables != N_FAT_TABLES) {
        printf("WARNING: Number of FAT tables is not %d!\n", N_FAT_TABLES);
    }
    sector_size = unsigned_bytes_to_int(boot_sector->sector_size, 2);
    if (sector_size != SECTORSIZE) {
        printf("WARNING: Sector size is not %d! It is %d.\n", SECTORSIZE, sector_size);
    }
    if (boot_sector->sec_per_clus != ASSUMED_SEC_PER_CLUS) {
        printf("WARNING: Sectors per cluster is not %d!\n", ASSUMED_SEC_PER_CLUS);
    }
    if (boot_sector->fat_length != boot_sector->fat32.length) {
        printf("WARNING: FAT length is not equal to FAT32 length!\n");
    }
    total_sectors = boot_sector->total_sect;
    fat_size = boot_sector->fat32.length;
    root_directory_cluster_offset = (reserved_sectors + fat_size * number_of_fat_tables) * SECTORSIZE;
    fat_table_offset = reserved_sectors * SECTORSIZE;
    root_directory_max_content_size = (N_ROOT_DIRECTORY_CLUSTERS * CLUSTERSIZE) / FILE_DIRECTORY_ENTRY_SIZE;

    // Calculate usable clusters size and usable fat table size
    usable_clusters_size = (total_sectors - reserved_sectors - fat_size * number_of_fat_tables) / sectors_per_cluster;
    if (usable_clusters_size > MAX_NUMBER_OF_CLUSTERS_FAT_TABLE) {
        usable_clusters_size = MAX_NUMBER_OF_CLUSTERS_FAT_TABLE;
    }
    usable_fat_table_size = fat_size * SECTORSIZE / FAT_TABLE_ENTRY_SIZE - 2 * FAT_TABLE_ENTRY_SIZE;
    if (usable_fat_table_size <= usable_clusters_size) {
        usable_clusters_size = usable_fat_table_size;
    } else {
        usable_fat_table_size = usable_clusters_size;
    }

    // Read the second argument 
    // if it is -l, list the contents of the root directory
    if (strcmp(argv[2], "-l") == 0) {
        int result = read_root_directory(fd, LIST_DIRECTORIES);
        if (result == FAILURE) {
            printf("Could not read root directory!\n");
        }
    }

    // if it is -r, read the file in binary or ASCII
    else if (strcmp(argv[2], "-r") == 0) {
        // Check if the user has entered the correct number of arguments
        if (argc < 4) {
            printf("%s", INVALID_ARGUMENTS);
            return 0;
        }

        // Read the file name and extension and check if the file name is valid
        // Set the file name to the global variable
        strcpy(input_file_name, argv[4]);
        int result = check_set_file_name(input_file_name);
        if (result == FAILURE) {
            printf("File name is invalid!\n");
            return 0;
        }

        // Read the file in binary or ASCII
        if (strcmp(argv[3], "-b") == 0) {
            // Read and print the file in binary
            read_file(fd, 1);
        } else if (strcmp(argv[3], "-a") == 0) {
            // Read and print the file in ASCII
            read_file(fd, 0);
        } else {
            printf("%s", INVALID_ARGUMENTS);
            return 0;
        }
    }

    // With the -c option, the program will create a file named <FILENAME> in the root directory.
    // This file will have a corresponding directory entry, an initial size of 0, 
    // and no blocks allocated for it initially.
    else if (strcmp(argv[2], "-c") == 0) {
        // Check if the user has entered the correct number of arguments
        if (argc < 4) {
            printf("%s", INVALID_ARGUMENTS);
            return 0;
        }

        // Read the file name and extension and check if the file name is valid 
        // Set the file name to the global variable
        strcpy(input_file_name, argv[3]);
        int result = check_set_file_name(input_file_name);
        if (result == FAILURE) {
            printf("File name is invalid!\n");
            return 0;
        }

        // Create a file named with given input in the root directory
        result = create_file_entry(fd);
        if (result == FAILURE) {
            printf("Could not create file entry!\n");
            return 0;
        }

        printf("File created successfully!\n");
    }

    // With the -w option, the program will write data into the file starting at the offset given in the fifth argument. 
    // The number of bytes to write is specified by the sixth argument.
    // The same data byte will be written in consecutive bytes. 
    // The content of the byte is given by the seventh argument, which is an unsigned integer between 0 and 255.    
    // For example, if we specify the data as 48 and the length as 10, then 10 consecutive
    // bytes in hexadecimal will be written: 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30.
    // This operation can overwrite existing data in the file and add new data, potentially allocating new free clusters to the file.
    else if (strcmp(argv[2], "-w") == 0) {
        // Check if the user has entered the correct number of arguments
        if (argc < 7) {
            printf("%s", INVALID_ARGUMENTS);
            return 0;
        }

        // Read the file name and extension and check if the file name is valid
        // Set the file name to the global variable
        strcpy(input_file_name, argv[3]);
        int result = check_set_file_name(input_file_name);
        if (result == FAILURE) {
            printf("File name is invalid!\n");
            return 0;
        }

        // Read the start offset, length, and the string
        int start_offset = atoi(argv[4]);
        int length = atoi(argv[5]);
        int data = atoi(argv[6]);

        // Write the bytes to the file
        result = write_bytes_to_file(fd, start_offset, length, data);
        if (result == FAILURE) {
            printf("Could not write bytes to file!\n");
            return 0;
        }
    }

    // With the -d option, your program will delete the file named <FILENAME> from the root directory
    // and free the blocks allocated for it in the FAT.
    else if (strcmp(argv[2], "-d") == 0) {
        // Check if the user has entered the correct number of arguments
        if (argc < 4) {
            printf("%s", INVALID_ARGUMENTS);
            return 0;
        }

        // Read the file name and extension and check if the file name is valid
        // Set the file name to the global variable
        strcpy(input_file_name, argv[3]);
        int result = check_set_file_name(input_file_name);
        if (result == FAILURE) {
            printf("File name is invalid!\n");
            return 0;
        }

        // Delete the file
        result = delete_file(fd);
        if (result == FAILURE) {
            printf("Could not delete file!\n");
            return 0;
        }
    } else {
        printf("%s", INVALID_ARGUMENTS);
    }

    close(fd);
}

// Write the bytes to the file starting at the given start offset and length with the given data
// This operation can overwrite existing data in the file and add new data, potentially allocating new free clusters to the file.
// Data is an unsigned integer between 0 and 255
// The same data byte will be written in consecutive bytes
// If the file is empty, the program will allocate a new cluster for the file and update the FAT table
// If the file is not empty, the program will find the cluster that contains the start offset and write the data
// If the length is larger than the remaining space in the cluster, the program will allocate a new cluster(s) and update the FAT table
int write_bytes_to_file(int fd, int start_offset, int length, int data) {
    // Find the entry with the given file name and extension
    int directory_entry_index = read_root_directory(fd, FIND_GIVEN_ENTRY);
    if (directory_entry_index == FAILURE) {
        printf("File not found!\n");
        return FAILURE;
    }

    // Check if the start offset is valid   
    if (start_offset < 0) {
        printf("Start offset is invalid!\n Start offset: %d\n", start_offset);
        return FAILURE;
    }

    // Check if the start offset is not larger than the file size
    if (start_offset > file_directory_entry->size) {
        printf("Start offset is larger than the file size! Start offset: %d, File size: %d\n", start_offset, file_directory_entry->size);
        return FAILURE;
    }

    // Find the current cluster size of the file and the clusters needed for the new data
    int file_cluster_size = file_directory_entry->size / CLUSTERSIZE + (file_directory_entry->size % CLUSTERSIZE != 0);
    int clusters_needed = (start_offset + length) / CLUSTERSIZE + ((start_offset + length) % CLUSTERSIZE != 0) - file_cluster_size;

    // Get the first cluster of the file by combining the high and low bytes
    unsigned int current_cluster = file_directory_entry->starthi << 16 | file_directory_entry->start;

    // Allocate new clusters if needed
    if (clusters_needed > 0) {
        // Find the last cluster of the file
        for (int i = 0; i < file_cluster_size - 1; i++) {
            current_cluster = get_next_FAT_table_entry(fd, current_cluster);
        }

        // Allocate new clusters
        for (int i = 0; i < clusters_needed; i++) {
            // Find the first free cluster in the FAT table
            int free_cluster = 0;
            for (int j = root_directory_cluster_number + N_ROOT_DIRECTORY_CLUSTERS; j < usable_clusters_size; j++) {
                int fat_table_entry_value = get_next_FAT_table_entry(fd, j);
                if (fat_table_entry_value == FAT_TABLE_FREE_CLUSTER_VALUE) {
                    free_cluster = j;
                    break;
                }
            }

            // Check if there is a free cluster
            if (free_cluster == 0) {
                printf("No free clusters available!\n");
                return FAILURE;
            }

            // Update the FAT table
            if (current_cluster == 0) {
                // Update the first cluster of the file
                file_directory_entry->starthi = free_cluster >> 16;
                file_directory_entry->start = free_cluster & 0xFFFF;
            } else {
                // Change the end value of the last cluster to the new cluster
                write_fat_table_entry(fd, current_cluster, free_cluster);
            }
            // Change the new cluster to the end of file value
            write_fat_table_entry(fd, free_cluster, FAT_TABLE_END_OF_FILE_VALUE);

            // NOTE: This part is not necessary
            /* // Refresh the current cluster by filling the new cluster with the zeros
            unsigned char cluster_buffer[CLUSTERSIZE];
            memset(cluster_buffer, 0, CLUSTERSIZE);
            write_cluster(fd, cluster_buffer, free_cluster); */

            // Update the current cluster
            current_cluster = free_cluster;
        }
    }

    // Update the file directory entry in the root directory

    // Increase the size of the file if the length + start offset is larger than the file size
    if (start_offset + length > file_directory_entry->size) {
        file_directory_entry->size = start_offset + length;
    }

    // Update the time and date of the file directory entry
    time_t current_time = time(NULL);
    struct tm* time_info = localtime(&current_time);
    file_directory_entry->time = (time_info->tm_hour << 11) | (time_info->tm_min << 5) | (time_info->tm_sec / 2);
    file_directory_entry->date = ((time_info->tm_year - 80) << 9) | ((time_info->tm_mon + 1) << 5) | time_info->tm_mday;
    file_directory_entry->adate = ((time_info->tm_year - 80) << 9) | ((time_info->tm_mon + 1) << 5) | time_info->tm_mday;

    // Update the file directory entry in the root directory
    int result = write_file_directory_entry(fd, directory_entry_index);
    if (result == FAILURE) {
        return FAILURE;
    }

    // Write the data to the file

    // Find the cluster that contains the start offset
    current_cluster = file_directory_entry->starthi << 16 | file_directory_entry->start;
    for (int i = 0; i < start_offset / CLUSTERSIZE; i++) {
        current_cluster = get_next_FAT_table_entry(fd, current_cluster);
    }

    // Find the offset in the cluster
    int cluster_offset = start_offset % CLUSTERSIZE;

    // Write the data to the file
    unsigned char cluster_buffer[CLUSTERSIZE];
    // Read the cluster
    read_cluster(fd, cluster_buffer, current_cluster);
    for (int i = 0; i < length; i++) {
        // Write the data to the cluster
        cluster_buffer[cluster_offset] = data;

        // Update the cluster offset
        cluster_offset++;

        // Check if the cluster offset is equal to the cluster size
        // If it is, write the cluster to the disk image and get the next cluster
        if (cluster_offset == CLUSTERSIZE) {
            // Write the cluster
            write_cluster(fd, cluster_buffer, current_cluster);

            cluster_offset = 0;
            current_cluster = get_next_FAT_table_entry(fd, current_cluster);

            // Read the cluster
            read_cluster(fd, cluster_buffer, current_cluster);
        }
    }

    // Write the last cluster to the disk image
    write_cluster(fd, cluster_buffer, current_cluster);

    printf("Bytes written to the file successfully!\n");
    return SUCCESS;
}

// Delete the file named with given input in the root directory, and free the blocks allocated for it in the FAT
int delete_file(int fd) {
    // Find the entry with the given file name and extension
    int directory_entry_index = read_root_directory(fd, FIND_GIVEN_ENTRY);
    if (directory_entry_index == FAILURE) {
        printf("File not found!\n");
        return FAILURE;
    }

    // Get the first cluster of the file by combining the high and low bytes
    unsigned int current_cluster = file_directory_entry->starthi << 16 | file_directory_entry->start;
    unsigned int next_cluster = get_next_FAT_table_entry(fd, current_cluster);

    // Free the blocks allocated for the file in the FAT in a loop
    while (current_cluster < FAT_TABLE_END_OF_FILE_VALUE && current_cluster > 1) {
        // Free the current cluster in the FAT table
        write_fat_table_entry(fd, current_cluster, FAT_TABLE_FREE_CLUSTER_VALUE);

        // Get the next cluster
        current_cluster = next_cluster;
        next_cluster = get_next_FAT_table_entry(fd, current_cluster);
    }

    // Delete the file directory entry by setting the first byte to 0xE5
    file_directory_entry->name[0] = 0xE5;
    // Calculate the offset
    off_t offset = root_directory_cluster_offset
        + directory_entry_index * FILE_DIRECTORY_ENTRY_SIZE;
    // Update the file directory entry in the root directory
    lseek(fd, offset, SEEK_SET);

    // Write the beginning of the file directory entry to the root directory
    int result = write(fd, file_directory_entry_raw, 2);
    // Write the rest of the file directory entry to the root directory
    fsync(fd);

    if (result < 0) {
        return FAILURE;
    }

    printf("File deleted successfully!\n");
    return SUCCESS;
}

// Creates a file named with given input in the root directory. This file will have a
// corresponding directory entry, an initial size of 0, and no blocks allocated for it initially.
int create_file_entry(int fd) {
    // Check if there exists a same file in the root directory
    int result = read_root_directory(fd, FIND_GIVEN_ENTRY);
    if (result != FAILURE) {
        printf("File already exists!\n");
        return FAILURE;
    }

    // Find the first free entry in the root directory
    int free_root_directory_entry_index = read_root_directory(fd, FIND_FREE_ENTRY);
    if (free_root_directory_entry_index == FAILURE) {
        printf("Root directory is full!\n");
        return FAILURE;
    }

    // Create a new file directory entry for FAT32 file system
    memset(file_directory_entry_raw, 0, FILE_DIRECTORY_ENTRY_SIZE);

    // First 8 bytes are for the file name, find the length of the file name until the dot
    // copy the file name to the file directory entry
    int is_dot_found = 0;
    int input_file_name_index = 0;
    for (int i = 0; i < FILENAME_SIZE; i++) {
        if (input_file_name[input_file_name_index] == '.') {
            is_dot_found = 1;
            input_file_name_index++;
        }
        if (is_dot_found) {
            file_directory_entry->name[i] = ' ';
        } else {
            file_directory_entry->name[i] = input_file_name[input_file_name_index++];
        }
    }
    // Copy the file extension to the file directory entry
    for (int i = FILENAME_SIZE; i < TOTAL_FILENAME_SIZE; i++) {
        if (input_file_name[input_file_name_index] == '\0') {
            break;
        }
        file_directory_entry->name[i] = input_file_name[input_file_name_index++];
    }

    // Set the attribute of the file directory entry to 0x20 as it is a file and not a directory
    // Directory entries are not supported in this program
    file_directory_entry->attr = 0x20;

    // Set the creation time, date, last access date
    time_t current_time = time(NULL);
    struct tm* time_info = localtime(&current_time);
    // Set the creation time
    file_directory_entry->ctime_cs = (time_info->tm_sec % 2) * 100;
    file_directory_entry->ctime = (time_info->tm_hour << 11) | (time_info->tm_min << 5) | (time_info->tm_sec / 2);
    // Set the creation date
    file_directory_entry->cdate = ((time_info->tm_year - 80) << 9) | ((time_info->tm_mon + 1) << 5) | time_info->tm_mday;
    // Set the last access date
    file_directory_entry->adate = ((time_info->tm_year - 80) << 9) | ((time_info->tm_mon + 1) << 5) | time_info->tm_mday;

    // Set high start cluster, initially set to 0
    file_directory_entry->starthi = 0;

    // Set last write time, date
    file_directory_entry->time = (time_info->tm_hour << 11) | (time_info->tm_min << 5) | (time_info->tm_sec / 2);
    file_directory_entry->date = ((time_info->tm_year - 80) << 9) | ((time_info->tm_mon + 1) << 5) | time_info->tm_mday;

    // Set start cluster, initially set to 0
    file_directory_entry->start = 0;

    // Set the size of the file, initially set to 0
    file_directory_entry->size = 0;

    // Write the file directory entry to the root directory
    return write_file_directory_entry(fd, free_root_directory_entry_index);
}

// Function to read the file in binary or ASCII, following the steps below:
// - Read the root directory to get the file directory entry
// - Get the first cluster of the file by combining the high and low bytes
// - Read the FAT table entry for the first cluster to get the next cluster
// - Start reading the file as a chain of clusters starting from the first cluster until the end of the file
// - Read cluster by cluster from the disk image and print the contents of the file in binary or ASCII in sectors
void read_file(int fd, int is_binary) {
    // Read the root directory to get the file directory entry
    int result = read_root_directory(fd, FIND_GIVEN_ENTRY);
    if (result == FAILURE) {
        printf("File not found!\n");
        return;
    }

    // Get the first cluster of the file by combining the high and low bytes
    unsigned int current_cluster = file_directory_entry->starthi << 16 | file_directory_entry->start;
    unsigned int next_cluster = get_next_FAT_table_entry(fd, current_cluster);

    // Start reading the file as a chain of clusters starting from the first cluster until the end of the file
    // Read cluster by cluster from the disk image
    unsigned char cluster_buffer[CLUSTERSIZE];
    for (int i = 0; i < file_directory_entry->size; i += CLUSTERSIZE) {
        // Check if the next cluster is the end of the file
        if (current_cluster >= FAT_TABLE_END_OF_FILE_VALUE) {
            printf("\n");
            break;
        }

        // Read the cluster
        read_cluster(fd, cluster_buffer, current_cluster);

        // Print the contents of the file in binary or ASCII in sectors
        // In binary form: It will display the content of the file in binary form on the screen. 
        // The content can be either binary or text. Each byte will be printed in hexadecimal form. 
        // With each line printing 16 bytes.The first hexadecimal number indicates the start
        // offset of that line in the file.
        // In ASCII form: It will display the content of the file in ASCII form on the screen.
        if (is_binary) {
            // Print the contents of the file in binary
            // Print the current sector
            for (int k = 0; k < CLUSTERSIZE; k++) {
                // For every 16 bytes, print the start offset of that line in the file
                if (k % 16 == 0) {
                    printf("%08X ", i + k);
                }
                // Print the content of the file in hexadecimal form
                printf("%02X ", cluster_buffer[k]);
                // Print a new line after every 16 bytes
                if ((k + 1) % 16 == 0) {
                    printf("\n");
                }

                // End of the file is reached so print a new line and break the loop
                if (i + k == file_directory_entry->size - 1) {
                    printf("\n");
                    break;
                }
            }

        } else {
            // Print the contents of the file in ASCII
            for (int k = 0; k < CLUSTERSIZE; k++) {
                printf("%c", cluster_buffer[k]);

                // End of the file is reached so print a new line and break the loop
                if (i + k == file_directory_entry->size - 1) {
                    printf("\n");
                    break;
                }
            }
        }

        // Get the next cluster
        current_cluster = next_cluster;
        next_cluster = get_next_FAT_table_entry(fd, current_cluster);
    }

    printf("\nSuccesfully read!\n");
}

// Function to get the next FAT table entry
// Read the FAT table entry for the given cluster number
int get_next_FAT_table_entry(int fd, unsigned int cluster_number) {
    // Calculate the offset
    off_t offset = fat_table_offset + cluster_number * FAT_TABLE_ENTRY_SIZE;
    lseek(fd, offset, SEEK_SET);

    // Read the FAT table entry
    int result = read(fd, fat_table_entry, FAT_TABLE_ENTRY_SIZE);

    if (result < 0) {
        return FAILURE;
    }

    // Convert the FAT table entry to an integer
    int next_cluster = unsigned_bytes_to_int(fat_table_entry, FAT_TABLE_ENTRY_SIZE);
    return next_cluster;
}

// Read the contents of the root directory which is stored in the second cluster
// For simplicity, we will assume that the root directory is one cluster in size
// Traverse every entry in the root directory and do the following:
// If the option option is set LIST_DIRECTORIES and valid, print the file name and extension
// If the option option is set FIND_FREE_ENTRY and empty, find the first free entry in the root directory and return the index of the entry
// Else(FIND_GIVEN_ENTRY), find the entry with the given file name and extension
// and set the file_directory_entry pointer to that entry, return the index of the entry, if not found return FAILURE
int read_root_directory(int fd, int option) {
    // Read the root directory from the disk image
    int result = read_cluster(fd, root_directory, root_directory_cluster_number);
    if (result < 0) {
        return FAILURE;
    }

    // Traverse every entry in the root directory
    // Skip the first entry because it is the volume label
    for (int i = 0; i < root_directory_max_content_size; i++) {
        // Read the file directory entry
        memcpy(file_directory_entry_raw, root_directory + i * FILE_DIRECTORY_ENTRY_SIZE, FILE_DIRECTORY_ENTRY_SIZE);
        file_directory_entry = (struct msdos_dir_entry*) file_directory_entry_raw;

        // Check if the file is a valid file
        if (file_directory_entry->name[0] == 0x00 || file_directory_entry->name[0] == 0xE5) {
            // This entry is free or deleted
            // If the option is FIND_FREE_ENTRY, return the index of first free entry
            if (option == FIND_FREE_ENTRY) {
                return i;
            }
        } else if (file_directory_entry->attr == 0x08) {
            // This entry is a volume label
            if (LIST_DIRECTORIES == option) {
                printf("Volume label: %s\n", file_directory_entry->name);
            }
        } else if (file_directory_entry->attr == 0x10) {
            // This entry is a directory
            // This project does not support directories
            printf("WARNING: Detected directory entry. Directories are not supported!\n");
            continue;
        } else if (file_directory_entry->attr == 0x0F) {
            // This entry is a long file name entry
            // This project does not support long file names
            printf("WARNING: Detected long file name entry. Long file name entries are not supported!\n");
            continue;
        } else if (file_directory_entry->attr == 0x20) {
            // If we are looking for a free entry, continue the loop, no need to check this entry
            if (option == FIND_FREE_ENTRY) {
                continue;
            }

            // This entry is a valid file
            // Get the file name and extension by inserting a dot between them
            // File name is 11 bytes long consisting of 8 bytes for the name and 3 bytes for the extension
            memset(total_file_name, '\0', TOTAL_FILENAME_SIZE + DOT_SIZE);

            // Loop through the file name and extension but only get the following characters
            // letters, digits, -, _,
            int file_name_index = 0;
            for (int j = 0; j < FILENAME_SIZE; j++) {
                if (isalnum(file_directory_entry->name[j]) || file_directory_entry->name[j] == '-'
                    || file_directory_entry->name[j] == '_') {
                    total_file_name[file_name_index++] = file_directory_entry->name[j];
                } else {
                    break;
                }
            }

            // Remember the index of the dot    
            int dot_index = file_name_index;
            // Increment the file name index by 1 for the dot
            file_name_index++;

            // Loop through the file extension same as the file name
            for (int j = FILENAME_SIZE; j < TOTAL_FILENAME_SIZE; j++) {
                if (isalnum(file_directory_entry->name[j]) || file_directory_entry->name[j] == '-'
                    || file_directory_entry->name[j] == '_') {
                    total_file_name[file_name_index++] = file_directory_entry->name[j];
                } else {
                    break;
                }
            }

            // Add a dot between the file name and extension if the extension is not empty 
            if (dot_index + 1 != file_name_index) {
                total_file_name[dot_index] = '.';
            }

            // Print the file name and extension if the LIST_DIRECTORIES option is set
            if (option == LIST_DIRECTORIES) {
                // Print the file name and extension and size of the file
                printf("%s %d\n", total_file_name, file_directory_entry->size);
            } else {
                // Find the entry with the given file name and extension
                // and set the file_directory_entry pointer to that entry
                // If the entry is not found, set the file_directory_entry pointer to NULL
                // If the entry is found, break the loop

                // Check if the file name and extension match the given file name and extension
                // If they match, return the index of the entry
                if (strcmp(total_file_name, input_file_name) == 0) {
                    return i;
                }
            }
        } else {
            // This entry is invalid
            printf("WARNING: Detected invalid entry!\n");
        }
    }

    // If the option is LIST_DIRECTORIES, return SUCCESS as the operation is successful
    if (option == LIST_DIRECTORIES) {
        return SUCCESS;
    } else {
        return FAILURE;
    }
}

// Read a cluster from the disk image
int read_cluster(int fd, unsigned char* buffer, unsigned int cluster_number) {
    // Calculate the offset
    off_t offset = root_directory_cluster_offset
        + ((cluster_number - 2) * sectors_per_cluster) * SECTORSIZE;
    lseek(fd, offset, SEEK_SET);

    // Read the cluster
    int result = read(fd, buffer, SECTORSIZE * sectors_per_cluster);

    if (result == SECTORSIZE * sectors_per_cluster) {
        return SUCCESS;
    } else {
        return FAILURE;
    }
}

// Read a sector from the disk image    
int read_sector(int fd, unsigned char* buffer, unsigned int sector_number) {
    // Calculate the offset
    off_t offset = sector_number * SECTORSIZE;
    lseek(fd, offset, SEEK_SET);

    // Read the sector
    int result = read(fd, buffer, SECTORSIZE);

    if (result == SECTORSIZE) {
        return SUCCESS;
    } else {
        return FAILURE;
    }
}

// Write a cluster to the disk image
int write_cluster(int fd, unsigned char* buffer, unsigned int cluster_number) {
    // Calculate the offset
    off_t offset = root_directory_cluster_offset
        + ((cluster_number - 2) * sectors_per_cluster) * SECTORSIZE;
    lseek(fd, offset, SEEK_SET);

    // Write the cluster
    int result = write(fd, buffer, SECTORSIZE * sectors_per_cluster);
    fsync(fd);

    if (result == SECTORSIZE * sectors_per_cluster) {
        return SUCCESS;
    } else {
        return FAILURE;
    }
}

// Write a sector to the disk image
int write_sector(int fd, unsigned char* buffer, unsigned int sector_number) {
    // Calculate the offset
    off_t offset = sector_number * SECTORSIZE;
    lseek(fd, offset, SEEK_SET);

    // Write the sector
    int result = write(fd, buffer, SECTORSIZE);
    fsync(fd);

    if (result == SECTORSIZE) {
        return SUCCESS;
    } else {
        return FAILURE;
    }
}

// Write a fat table entry to the disk image
int write_fat_table_entry(int fd, unsigned int cluster_number, unsigned int value) {
    // Calculate the offset
    off_t offset = fat_table_offset + cluster_number * FAT_TABLE_ENTRY_SIZE;
    lseek(fd, offset, SEEK_SET);

    // Convert the value to 4 bytes in little-endian order
    int_to_unsigned_bytes(value, fat_table_entry);

    // Write the FAT table
    int result = write(fd, fat_table_entry, FAT_TABLE_ENTRY_SIZE);
    fsync(fd);

    if (result == FAT_TABLE_ENTRY_SIZE) {
        return SUCCESS;
    } else {
        return FAILURE;
    }
}

// Write the file directory entry to the root directory 
int write_file_directory_entry(int fd, int directory_entry_index) {
    // Calculate the offset
    off_t offset = root_directory_cluster_offset
        + directory_entry_index * FILE_DIRECTORY_ENTRY_SIZE;
    lseek(fd, offset, SEEK_SET);

    // Write the file directory entry to the root directory
    int result = write(fd, file_directory_entry_raw, FILE_DIRECTORY_ENTRY_SIZE);
    fsync(fd);

    if (result < 0) {
        return FAILURE;
    }

    return SUCCESS;
}

// Function to print the help message about the usage of the program
void print_help_message() {
    printf("Usage: fatmod <diskname> <options>\n");
    printf("Options:\n");
    printf("-h: Print this help message\n");
    printf("-l: List the contents of the root directory\n");
    printf("-c <file>: Create a new file with size 0\n");
    printf("-w <file> <offset> <length> <data>: Write data[0-255] to file\n");
    printf("-r -b <file>: Read and print the file in binary\n");
    printf("-r -a <file>: Read and print the file in ASCII\n");
    printf("-d <file>: Delete the file\n");
}

// Check if the value is negative, if it is, convert it to a positive value
// This function is used to handle the overflow of the char type
int char_overflow_check(int value) {
    if (value < 0) {
        value += 256;
    }
    return value;
}

// Convert 4 bytes to an integer in little-endian order
int bytes_to_int(char* bytes, int length) {
    int value = 0;
    for (int i = 0; i < length; i++) {
        value |= (int) (char_overflow_check(bytes[i])) << (i * 8);
    }
    return value;
}

// Convert 4 bytes to an integer in little-endian order
int unsigned_bytes_to_int(unsigned char* bytes, int length) {
    int value = 0;
    for (int i = 0; i < length; i++) {
        value |= (int) (char_overflow_check(bytes[i])) << (i * 8);
    }
    return value;
}

// Convert an integer to 4 bytes in little-endian order
// Fill the 4 bytes with the integer value
void int_to_bytes(int val, char* bytes) {
    bytes[0] = (char) (val & 0xFF);
    bytes[1] = (char) ((val >> 8) & 0xFF);
    bytes[2] = (char) ((val >> 16) & 0xFF);
    bytes[3] = (char) ((val >> 24) & 0xFF);
}

// Convert an integer to 4 bytes in little-endian order
// Fill the 4 bytes with the integer value
void int_to_unsigned_bytes(int val, unsigned char* bytes) {
    bytes[0] = (char) (val & 0xFF);
    bytes[1] = (char) ((val >> 8) & 0xFF);
    bytes[2] = (char) ((val >> 16) & 0xFF);
    bytes[3] = (char) ((val >> 24) & 0xFF);
}

// Check if the file name is valid
int check_set_file_name(char* str) {
    // Check if the file name is empty
    if (strlen(str) > TOTAL_FILENAME_SIZE) {
        return FAILURE;
    }

    // Check if the file name or extension is empty by checking the dot
    if (str[0] == '.' || str[strlen(str) - 1] == '.') {
        return FAILURE;
    }

    // Check if the file name or extension is empty by checking the space
    if (str[0] == ' ' || str[strlen(str) - 1] == ' ') {
        return FAILURE;
    }

    // Check the characters
    for (int i = 0; i < strlen(str); i++) {
        str[i] = toupper(str[i]);
        if (!isalnum(str[i]) && str[i] != '-' && str[i] != '_' && str[i] != '.') {
            return FAILURE;
        }
    }

    return SUCCESS;
}

// Get the length of the ascii string by traversing the string
// if the character is not an uppercase letter, lowercase letter, digit, -, _, or ., 
// increase the length of the string by 1
int get_length_of_file_name(char* str) {
    int length = 0;
    for (int i = 0; i < strlen(str); i++) {
        if (!isalnum(str[i]) && str[i] != '-' && str[i] != '_' && str[i] != '.') {
            break;
        }
        length++;
    }
    return length;
}
