// Görkem Kadir Solun 10.05.2024
// 
// This is project/program to access and modify a FAT32 disk
// image(FAT32 volume). The disk image is stored as a regular Linux file, which
// will simulate a disk formatted with the FAT32 file system. This program opens
// the Linux file using the standard open() system call and access it directly in raw
// mode, utilizing the read() and write() system calls, without mounting the FAT32 file system.
// The program will be named fatmod. Through various options, it will interact with a file system image, enabling reading and writing of files.

// TODOs:
// Convert read by cluster to read by sector // NOTE: This may not be necessary

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
#define FALSE 0
#define TRUE 1
#define INVALID_ARGUMENTS "Invalid arguments. Please enter -h for help\n"
#define FAT_TABLE_END_OF_FILE_VALUE 0x0FFFFFF8

#define SECTORSIZE 512   // bytes
#define CLUSTERSIZE  1024  // bytes
#define FILENAME_SIZE 8 // bytes
#define FILE_EXTENSION_SIZE 3 // bytes
#define DOT_SIZE 1 // bytes
#define FAT_TABLE_ENTRY_SIZE 4 // bytes
#define TOTAL_FILENAME_SIZE FILE_EXTENSION_SIZE + FILENAME_SIZE // bytes    
#define FILE_DIRECTORY_ENTRY_SIZE 32 // bytes

#define N_RESERVED_SECTORS 32 // sectors
#define N_ROOT_DIRECTORY_CLUSTERS 1 // clusters
#define N_FAT_TABLES 1 // number of FAT tables
#define ASSUMED_CLUSTER_SIZE 2 // sectors   
#define ASSUMED_SEC_PER_CLUS CLUSTERSIZE/SECTORSIZE // sectors
#define ASSUMED_ROOT_DIRECTORY_CLUSTER 2 // cluster number

// ___Function Prototypes___

// Main Functions
// read the root directory from the disk image which is stored in the second cluster
void read_root_directory(int fd, int is_list_directories);
// read the file in binary or ASCII
void read_file(int fd, int is_binary);
void print_help_message();

// Helper Functions 
int read_sector(int fd, unsigned char* buffer, unsigned int snum);
int write_sector(int fd, unsigned char* buffer, unsigned int snum);
int read_cluster(int fd, unsigned char* buffer, unsigned int cluster_number);
int get_next_FAT_table_entry(int fd, unsigned int cluster_number);
int char_overflow_check(int value);
int bytes_to_int_little_endian(char* bytes);
void int_to_bytes_little_endian(int val, char* bytes);
void to_upper(char* str);
int get_length_of_file_name(char* str);



// ___Global Variables___
int reserved_sectors;
int fat_size; // in sectors 
int number_of_fat_tables;
int fat_table_offset; // in sectors
int root_directory_cluster_number;
int sectors_per_cluster;
unsigned char boot_sector_raw[SECTORSIZE];
unsigned char root_directory[CLUSTERSIZE];
unsigned char file_directory_entry_raw[FILE_DIRECTORY_ENTRY_SIZE];
unsigned char fat_table_entry[4];
char input_file_name[TOTAL_FILENAME_SIZE];

struct fat_boot_sector* boot_sector;
struct msdos_dir_entry* file_directory_entry;


// ___Main Functions Start Here___

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
    // BELOW ARE FOR TESTING PURPOSES
    /* char* test_argv[] = { "fatmod", "disk1", "-l" };
    argc = 3; */
    /* char* test_argv[] = { "fatmod", "disk1", "-r", "-b", "file1.bin" };
    argc = 5; */
    char* test_argv[] = { "fatmod", "disk1", "-r", "-a", "file4.txt" };
    argc = 5;
    argv = test_argv;

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
    fat_size = boot_sector->fat32.length;
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


    // Read the second argument 
    // if it is -l, list the contents of the root directory
    if (strcmp(argv[2], "-l") == 0) {
        read_root_directory(fd, 1);
    } else if (strcmp(argv[2], "-r") == 0) {
        // Check if the user has entered the correct number of arguments
        if (argc < 4) {
            printf("%s", INVALID_ARGUMENTS);
            return 0;
        }

        // Read the file name and extension
        strcpy(input_file_name, argv[4]);
        to_upper(input_file_name);

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
    } else if (strcmp(argv[2], "-c") == 0) {

        // Create a new file
        // Read the third argument
        // Create a new file
    } else if (strcmp(argv[2], "-w") == 0) {

        // Write string to file
        // Read the third argument
        // Read the fourth argument
        // Read the fifth argument
        // Read the sixth argument
        // Write the string to the file
    } else if (strcmp(argv[2], "-d") == 0) {
        // Delete the file
        // Read the third argument
        // Delete the file
    } else {
        printf("%s", INVALID_ARGUMENTS);
    }

    close(fd);
}

// Function to read the file in binary or ASCII, following the steps below:
// - Read the root directory to get the file directory entry
// - Get the first cluster of the file by combining the high and low bytes
// - Read the FAT table entry for the first cluster to get the next cluster
// - Start reading the file as a chain of clusters starting from the first cluster until the end of the file
// - Read cluster by cluster from the disk image and print the contents of the file in binary or ASCII in sectors
void read_file(int fd, int is_binary) {
    // Read the root directory to get the file directory entry
    read_root_directory(fd, 0);
    if (file_directory_entry->name[0] == 0x00 || file_directory_entry->name[0] == 0xE5) {
        printf("File not found!\n");
        return;
    }

    // BELOW ARE FOR TESTING PURPOSES
    struct msdos_dir_entry* temp = file_directory_entry;
    temp = (struct msdos_dir_entry*) file_directory_entry_raw;
    printf("\nStart: %d\n", file_directory_entry->start);
    printf("\nStartHi: %d\n", file_directory_entry->starthi);
    printf("\nSize: %d\n", file_directory_entry->size);


    // Get the first cluster of the file by combining the high and low bytes
    unsigned int current_cluster = file_directory_entry->starthi << 16 | file_directory_entry->start;
    unsigned int next_cluster = get_next_FAT_table_entry(fd, current_cluster);

    // BELOW ARE FOR TESTING PURPOSES
    printf("\nCurrent Cluster: %d\n", current_cluster);
    printf("\nNext Cluster: %d\n", next_cluster);

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
        for (int j = 0; j < CLUSTERSIZE / SECTOR_SIZE; j++) {
            // Print the contents of the file in binary
            if (is_binary) {
                // Print the current sector
                for (int k = 0; k < SECTORSIZE; k++) {
                    // For every 16 bytes, print the start offset of that line in the file
                    if (k % 16 == 0) {
                        printf("%08X ", i + j * SECTORSIZE + k);
                    }
                    // Print the content of the file in hexadecimal form
                    printf("%02X ", cluster_buffer[j * SECTORSIZE + k]);
                    // Print a new line after every 16 bytes
                    if ((k + 1) % 16 == 0) {
                        printf("\n");
                    }
                }

            } else {
                // Print the contents of the file in ASCII
                for (int k = 0; k < SECTORSIZE; k++) {
                    printf("%c", cluster_buffer[j * SECTORSIZE + k]);
                }
            }
        }

        // Get the next cluster
        current_cluster = next_cluster;
        next_cluster = get_next_FAT_table_entry(fd, current_cluster);
    }


}

// Function to get the next FAT table entry
// Read the FAT table entry for the given cluster number
int get_next_FAT_table_entry(int fd, unsigned int cluster_number) {
    // Calculate the offset
    off_t offset = reserved_sectors * SECTORSIZE + cluster_number * FAT_TABLE_ENTRY_SIZE;
    lseek(fd, offset, SEEK_SET);

    // Read the FAT table entry
    int n = read(fd, fat_table_entry, 4);

    if (n < 0) {
        return FAILURE;
    }

    // Convert the FAT table entry to an integer
    int next_cluster = bytes_to_int_little_endian(fat_table_entry);
    return next_cluster;
}

// Read the contents of the root directory
// Read the root directory from the disk image which is stored in the second cluster
// For simplicity, we will assume that the root directory is one cluster in size
// Traverse every entry in the root directory
// If the is_list_directories flag is set, print the file name and extension
// Else, find the entry with the given file name and extension
void read_root_directory(int fd, int is_list_directories) {
    // Read the root directory from the disk image
    read_cluster(fd, root_directory, root_directory_cluster_number);

    // Traverse every entry in the root directory
    // Skip the first entry because it is the volume label
    for (int i = 1; i < CLUSTERSIZE / FILE_DIRECTORY_ENTRY_SIZE; i++) {
        // Read the file directory entry
        memcpy(file_directory_entry_raw, root_directory + i * FILE_DIRECTORY_ENTRY_SIZE, FILE_DIRECTORY_ENTRY_SIZE);
        file_directory_entry = (struct msdos_dir_entry*) file_directory_entry_raw;

        // Check if the file is a valid file
        if (file_directory_entry->name[0] == 0x00 || file_directory_entry->name[0] == 0xE5) {
            // This entry is free or deleted
            // TODO: handle this case
            continue;
        } else if (file_directory_entry->attr == 0x0F) {
            // This entry is a long file name entry
            // This project does not support long file names
            continue;
        } else {
            // This entry is a valid file
            // Get the file name and extension by inserting a dot between them
            // File name is 11 bytes long consisting of 8 bytes for the name and 3 bytes for the extension
            char total_file_name[TOTAL_FILENAME_SIZE + DOT_SIZE];
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
            // Add a dot between the file name and extension
            total_file_name[file_name_index++] = '.';

            // Loop through the file extension same as the file name
            for (int j = FILENAME_SIZE; j < TOTAL_FILENAME_SIZE; j++) {
                if (isalnum(file_directory_entry->name[j]) || file_directory_entry->name[j] == '-'
                    || file_directory_entry->name[j] == '_') {
                    total_file_name[file_name_index++] = file_directory_entry->name[j];
                } else {
                    break;
                }
            }

            // Print the file name and extension if the is_list_directories flag is set
            if (is_list_directories) {
                // Print the file name and extension
                printf("%s\n", total_file_name);
            } else {
                // Find the entry with the given file name and extension
                // and set the file_directory_entry pointer to that entry
                // If the entry is not found, set the file_directory_entry pointer to NULL
                // If the entry is found, break the loop

                // Check if the file name and extension match the given file name and extension
                // If they match, break the loop
                // If they do not match, set the file_directory_entry pointer to NULL
                if (strcmp(total_file_name, input_file_name) == 0) {
                    break;
                }
            }
        }
    }
}

// Function to read a cluster from the disk image
int read_cluster(int fd, unsigned char* buffer, unsigned int cluster_number) {
    // Calculate the offset
    off_t offset = (reserved_sectors + fat_size * boot_sector->fats + (cluster_number - 2) * sectors_per_cluster) * SECTORSIZE;
    lseek(fd, offset, SEEK_SET);

    // Read the cluster
    int n = read(fd, buffer, SECTORSIZE * sectors_per_cluster);

    if (n == SECTORSIZE * sectors_per_cluster) {
        return SUCCESS;
    } else {
        return FAILURE;
    }
}

// Function to read a sector from the disk image    
int read_sector(int fd, unsigned char* buffer, unsigned int sector_number) {
    // Calculate the offset
    off_t offset = sector_number * SECTORSIZE;
    lseek(fd, offset, SEEK_SET);

    // Read the sector
    int n = read(fd, buffer, SECTORSIZE);

    if (n == SECTORSIZE) {
        return SUCCESS;
    } else {
        return FAILURE;
    }
}

// Function to write a sector to the disk image
int write_sector(int fd, unsigned char* buffer, unsigned int sector_number) {
    // Calculate the offset
    off_t offset = sector_number * SECTORSIZE;
    lseek(fd, offset, SEEK_SET);

    // Write the sector
    int n = write(fd, buffer, SECTORSIZE);
    fsync(fd);

    if (n == SECTORSIZE) {
        return SUCCESS;
    } else {
        return FAILURE;
    }
}

void print_help_message() {
    printf("Usage: fatmod <diskname> <options>\n");
    printf("Options:\n");
    printf("-h: Print this help message\n");
    printf("-l: List the contents of the root directory\n");
    printf("-c <file>: Create a new file\n");
    printf("-w <file> <start> <length> <string>: Write string to file\n");
    printf("-r -b <file>: Read and print the file in binary\n");
    printf("-r -a <file>: Read and print the file in ASCII\n");
    printf("-d <file>: Delete the file\n");
}

// ___Helper Functions Start Here___

// Check if the value is negative, if it is, convert it to a positive value
// This function is used to handle the overflow of the char type
int char_overflow_check(int value) {
    if (value < 0) {
        value += 256;
    }
    return value;
}

// Convert 4 bytes to an integer in little-endian order
int bytes_to_int_little_endian(char* bytes) {
    return (int) (char_overflow_check(bytes[0])) |
        (int) (char_overflow_check(bytes[1])) << 8 |
        (int) (char_overflow_check(bytes[2])) << 16 |
        (int) (char_overflow_check(bytes[3])) << 24;
}

// Convert an integer to 4 bytes in little-endian order
void int_to_bytes_little_endian(int val, char* bytes) {
    bytes[0] = (char) (val & 0xFF);
    bytes[1] = (char) ((val >> 8) & 0xFF);
    bytes[2] = (char) ((val >> 16) & 0xFF);
    bytes[3] = (char) ((val >> 24) & 0xFF);
}

// Conver to capital letters
void to_upper(char* str) {
    for (int i = 0; i < strlen(str); i++) {
        str[i] = toupper(str[i]);
    }
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
