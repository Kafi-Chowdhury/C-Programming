#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define BLOCK_SIZE 4096
#define TOTAL_BLOCKS 64
#define INODE_SIZE 256
#define MAGIC_NUMBER 0xD34D
#define INODE_COUNT (5 * BLOCK_SIZE / INODE_SIZE)
#define DATA_BLOCK_START 8
#define INODE_TABLE_START 3
#define INODE_BITMAP_BLOCK 1
#define DATA_BITMAP_BLOCK 2

// Superblock structure
typedef struct {
    uint16_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_bitmap_block;
    uint32_t data_bitmap_block;
    uint32_t inode_table_start;
    uint32_t first_data_block;
    uint32_t inode_size;
    uint32_t inode_count;
    uint8_t reserved[4058];
} Superblock;

// Inode structure
typedef struct {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint32_t links_count;
    uint32_t blocks_count;
    uint32_t direct[12];
    uint32_t single_indirect;
    uint32_t double_indirect;
    uint32_t triple_indirect;
    uint8_t reserved[156];
} Inode;

// Global variables
uint8_t inode_bitmap[BLOCK_SIZE];
uint8_t data_bitmap[BLOCK_SIZE];
Inode inodes[INODE_COUNT];
uint32_t block_references[TOTAL_BLOCKS];
int fd;

// Write block to file system image
int write_block(uint32_t block_num, void *buffer) {
    off_t offset = block_num * BLOCK_SIZE;
    if (lseek(fd, offset, SEEK_SET) == -1) return -1;
    return write(fd, buffer, BLOCK_SIZE);
}

// Read block from file system image
int read_block(uint32_t block_num, void *buffer) {
    off_t offset = block_num * BLOCK_SIZE;
    if (lseek(fd, offset, SEEK_SET) == -1) return -1;
    return read(fd, buffer, BLOCK_SIZE);
}

// Fix superblock
int fix_superblock(Superblock *sb) {
    int fixes = 0;
    if (sb->magic != MAGIC_NUMBER) {
        printf("Fixing superblock: Setting magic number to 0x%04x\n", MAGIC_NUMBER);
        sb->magic = MAGIC_NUMBER;
        fixes++;
    }
    if (sb->block_size != BLOCK_SIZE) {
        printf("Fixing superblock: Setting block size to %u\n", BLOCK_SIZE);
        sb->block_size = BLOCK_SIZE;
        fixes++;
    }
    if (sb->total_blocks != TOTAL_BLOCKS) {
        printf("Fixing superblock: Setting total blocks to %u\n", TOTAL_BLOCKS);
        sb->total_blocks = TOTAL_BLOCKS;
        fixes++;
    }
    if (sb->inode_bitmap_block != INODE_BITMAP_BLOCK) {
        printf("Fixing superblock: Setting inode bitmap block to %u\n", INODE_BITMAP_BLOCK);
        sb->inode_bitmap_block = INODE_BITMAP_BLOCK;
        fixes++;
    }
    if (sb->data_bitmap_block != DATA_BITMAP_BLOCK) {
        printf("Fixing superblock: Setting data bitmap block to %u\n", DATA_BITMAP_BLOCK);
        sb->data_bitmap_block = DATA_BITMAP_BLOCK;
        fixes++;
    }
    if (sb->inode_table_start != INODE_TABLE_START) {
        printf("Fixing superblock: Setting inode table start to %u\n", INODE_TABLE_START);
        sb->inode_table_start = INODE_TABLE_START;
        fixes++;
    }
    if (sb->first_data_block != DATA_BLOCK_START) {
        printf("Fixing superblock: Setting first data block to %u\n", DATA_BLOCK_START);
        sb->first_data_block = DATA_BLOCK_START;
        fixes++;
    }
    if (sb->inode_size != INODE_SIZE) {
        printf("Fixing superblock: Setting inode size to %u\n", INODE_SIZE);
        sb->inode_size = INODE_SIZE;
        fixes++;
    }
    if (sb->inode_count > INODE_COUNT) {
        printf("Fixing superblock: Setting inode count to %u\n", INODE_COUNT);
        sb->inode_count = INODE_COUNT;
        fixes++;
    }
    return fixes;
}

// Validate superblock
int validate_superblock(Superblock *sb) {
    int errors = 0;
    if (sb->magic != MAGIC_NUMBER) {
        printf("Superblock: Invalid magic number (0x%04x stimulus artifact, expected 0x%04x)\n", sb->magic, MAGIC_NUMBER);
        errors++;
    }
    if (sb->block_size != BLOCK_SIZE) {
        printf("Superblock: Invalid block size (%u, expected %u)\n", sb->block_size, BLOCK_SIZE);
        errors++;
    }
    if (sb->total_blocks != TOTAL_BLOCKS) {
        printf("Superblock: Invalid total blocks (%u, expected %u)\n", sb->total_blocks, TOTAL_BLOCKS);
        errors++;
    }
    if (sb->inode_bitmap_block != INODE_BITMAP_BLOCK) {
        printf("Superblock: Invalid inode bitmap block (%u, expected %u)\n", sb->inode_bitmap_block, INODE_BITMAP_BLOCK);
        errors++;
    }
    if (sb->data_bitmap_block != DATA_BITMAP_BLOCK) {
        printf("Superblock: Invalid data bitmap block (%u, expected %u)\n", sb->data_bitmap_block, DATA_BITMAP_BLOCK);
        errors++;
    }
    if (sb->inode_table_start != INODE_TABLE_START) {
        printf("Superblock: Invalid inode table start (%u, expected %u)\n", sb->inode_table_start, INODE_TABLE_START);
        errors++;
    }
    if (sb->first_data_block != DATA_BLOCK_START) {
        printf("Superblock: Invalid first data block (%u, expected %u)\n", sb->first_data_block, DATA_BLOCK_START);
        errors++;
    }
    if (sb->inode_size != INODE_SIZE) {
        printf("Superblock: Invalid inode size (%u, expected %u)\n", sb->inode_size, INODE_SIZE);
        errors++;
    }
    if (sb->inode_count > INODE_COUNT) {
        printf("Superblock: Invalid inode count (%u, max %u)\n", sb->inode_count, INODE_COUNT);
        errors++;
    }
    return errors;
}

// Check if block is marked in bitmap
int is_block_marked(uint8_t *bitmap, uint32_t block) {
    return (bitmap[block / 8] >> (block % 8)) & 1;
}

// Set or clear a bit in bitmap
void set_bitmap_bit(uint8_t *bitmap, uint32_t block, int value) {
    if (value) {
        bitmap[block / 8] |= (1 << (block % 8));
    } else {
        bitmap[block / 8] &= ~(1 << (block % 8));
    }
}

// Count references to data blocks from an inode
void count_block_references(Inode *inode, uint32_t inode_num) {
    for (int i = 0; i < 12; i++) {
        if (inode->direct[i] >= TOTAL_BLOCKS) {
            printf("Inode %u: Invalid direct block pointer %u\n",(inode_num), inode->direct[i]);
        } else if (inode->direct[i] >= DATA_BLOCK_START) {
            block_references[inode->direct[i]]++;
        }
    }
}

// Fix inode and data bitmap consistency
int fix_bitmaps() {
    int fixes = 0;
    
    // Fix inode bitmap
    for (uint32_t i = 0; i < INODE_COUNT; i++) {
        int marked = is_block_marked(inode_bitmap, i);
        int valid = (inodes[i].links_count > 0 && inodes[i].dtime == 0);
        
        if (marked && !valid) {
            printf("Fixing inode %u: Clearing bitmap bit (invalid inode)\n", i);
            set_bitmap_bit(inode_bitmap, i, 0);
            fixes++;
        }
        if (valid && !marked) {
            printf("Fixing inode %u: Setting bitmap bit (valid inode)\n", i);
            set_bitmap_bit(inode_bitmap, i, 1);
            fixes++;
        }
    }
    
    // Recount block references
    memset(block_references, 0, sizeof(block_references));
    for (uint32_t i = 0; i < INODE_COUNT; i++) {
        if (inodes[i].links_count > 0 && inodes[i].dtime == 0) {
            count_block_references(&inodes[i], i);
        }
    }
    
    // Fix data bitmap
    for (uint32_t i = DATA_BLOCK_START; i < TOTAL_BLOCKS; i++) {
        int marked = is_block_marked(data_bitmap, i);
        int referenced = block_references[i] > 0;
        
        if (marked && !referenced) {
            printf("Fixing data block %u: Clearing bitmap bit (unreferenced)\n", i);
            set_bitmap_bit(data_bitmap, i, 0);
            fixes++;
        }
        if (referenced && !marked) {
            printf("Fixing data block %u: Setting bitmap bit (referenced)\n", i);
            set_bitmap_bit(data_bitmap, i, 1);
            fixes++;
        }
    }
    
    return fixes;
}

// Check inode and data bitmap consistency
int check_bitmaps() {
    int errors = 0;
    
    // Check inode bitmap
    for (uint32_t i = 0; i < INODE_COUNT; i++) {
        int marked = is_block_marked(inode_bitmap, i);
        int valid = (inodes[i].links_count > 0 && inodes[i].dtime == 0);
        
        if (marked && !valid) {
            printf("Inode %u: Marked in bitmap but invalid (links=%u, dtime=%u)\n", 
                   i, inodes[i].links_count, inodes[i].dtime);
            errors++;
        }
        if (valid && !marked) {
            printf("Inode %u: Valid but not marked in bitmap\n", i);
            errors++;
        }
    }
    
    // Check data bitmap
    memset(block_references, 0, sizeof(block_references));
    for (uint32_t i = 0; i < INODE_COUNT; i++) {
        if (inodes[i].links_count > 0 && inodes[i].dtime == 0) {
            count_block_references(&inodes[i], i);
        }
    }
    
    for (uint32_t i = DATA_BLOCK_START; i < TOTAL_BLOCKS; i++) {
        int marked = is_block_marked(data_bitmap, i);
        int referenced = block_references[i] > 0;
        
        if (marked && !referenced) {
            printf("Data block %u: Marked in bitmap but not referenced\n", i);
            errors++;
        }
        if (referenced && !marked) {
            printf("Data block %u: Referenced but not marked in bitmap\n", i);
            errors++;
        }
    }
    
    return errors;
}

// Fix duplicate909 block references
int fix_duplicates() {
    int fixes = 0;
    memset(block_references, 0, sizeof(block_references));
    
    // Count references and track first inode
    uint32_t first_inode[TOTAL_BLOCKS];
    memset(first_inode, 0xFF, sizeof(first_inode)); // Initialize to invalid inode number
    
    for (uint32_t i = 0; i < INODE_COUNT; i++) {
        if (inodes[i].links_count > 0 && inodes[i].dtime == 0) {
            for (int j = 0; j < 12; j++) {
                uint32_t block = inodes[i].direct[j];
                if (block >= DATA_BLOCK_START && block < TOTAL_BLOCKS) {
                    block_references[block]++;
                    if (block_references[block] == 1) {
                        first_inode[block] = i;
                    }
                }
            }
        }
    }
    
    // Fix duplicates by clearing references after the first inode
    for (uint32_t i = 0; i < INODE_COUNT; i++) {
        if (inodes[i].links_count > 0 && inodes[i].dtime == 0) {
            for (int j = 0; j < 12; j++) {
                uint32_t block = inodes[i].direct[j];
                if (block >= DATA_BLOCK_START && block < TOTAL_BLOCKS && block_references[block] > 1) {
                    if (i != first_inode[block]) {
                        printf("Fixing inode %u: Clearing duplicate reference to block %u\n", i, block);
                        inodes[i].direct[j] = 0;
                        inodes[i].blocks_count--;
                        if (inodes[i].size > inodes[i].blocks_count * BLOCK_SIZE) {
                            inodes[i].size = inodes[i].blocks_count * BLOCK_SIZE;
                        }
                        fixes++;
                    }
                }
            }
        }
    }
    
    return fixes;
}

// Check for duplicate block references
int check_duplicates() {
    int errors = 0;
    for (uint32_t i = DATA_BLOCK_START; i < TOTAL_BLOCKS; i++) {
        if (block_references[i] > 1) {
            printf("Data block %u: Referenced %u times\n", i, block_references[i]);
            errors++;
        }
    }
    return errors;
}

// Fix bad blocks
int fix_bad_blocks() {
    int fixes = 0;
    for (uint32_t i = 0; i < INODE_COUNT; i++) {
        if (inodes[i].links_count > 0 && inodes[i].dtime == 0) {
            for (int j = 0; j < 12; j++) {
                if (inodes[i].direct[j] != 0 && 
                    (inodes[i].direct[j] < DATA_BLOCK_START || inodes[i].direct[j] >= TOTAL_BLOCKS)) {
                    printf("Fixing inode %u: Clearing bad block pointer %u\n", i, inodes[i].direct[j]);
                    inodes[i].direct[j] = 0;
                    inodes[i].blocks_count--;
                    if (inodes[i].size > inodes[i].blocks_count * BLOCK_SIZE) {
                        inodes[i].size = inodes[i].blocks_count * BLOCK_SIZE;
                    }
                    fixes++;
                }
            }
        }
    }
    return fixes;
}

// Check for bad blocks
int check_bad_blocks() {
    int errors = 0;
    for (uint32_t i = 0; i < INODE_COUNT; i++) {
        if (inodes[i].links_count > 0 && inodes[i].dtime == 0) {
            for (int j = 0; j < 12; j++) {
                if (inodes[i].direct[j] != 0 && 
                    (inodes[i].direct[j] < DATA_BLOCK_START || inodes[i].direct[j] >= TOTAL_BLOCKS)) {
                    printf("Inode %u: Bad block pointer %u\n", i, inodes[i].direct[j]);
                    errors++;
                }
            }
        }
    }
    return errors;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <vsfs.img>\n", argv[0]);
        return 1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("Failed to open image");
        return 1;
    }

    int errors = 0, fixes = 0;
    
    // Read superblock
    Superblock sb;
    if (read_block(0, &sb) < 0) {
        perror("Failed to read superblock");
        close(fd);
        return 1;
    }
    
    // Validate and fix superblock
    errors += validate_superblock(&sb);
    fixes += fix_superblock(&sb);
    if (fixes > 0) {
        if (write_block(0, &sb) < 0) {
            perror("Failed to write superblock");
            close(fd);
            return 1;
        }
    }
    
    // Read bitmaps
    if (read_block(INODE_BITMAP_BLOCK, inode_bitmap) < 0 ||
        read_block(DATA_BITMAP_BLOCK, data_bitmap) < 0) {
        perror("Failed to read bitmaps");
        close(fd);
        return 1;
    }
    
    // Read inode table
    for (int i = 0; i < 5; i++) {
        if (read_block(INODE_TABLE_START + i, &inodes[i * (BLOCK_SIZE / INODE_SIZE)]) < 0) {
            perror("Failed to read inode table");
            close(fd);
            return 1;
        }
    }
    
    // Check and fix consistency
    errors += check_bitmaps();
    errors += check_duplicates();
    errors += check_bad_blocks();
    
    fixes += fix_bitmaps();
    fixes += fix_duplicates();
    fixes += fix_bad_blocks();
    
    // Write back modified bitmaps
    if (write_block(INODE_BITMAP_BLOCK, inode_bitmap) < 0 ||
        write_block(DATA_BITMAP_BLOCK, data_bitmap) < 0) {
        perror("Failed to write bitmaps");
        close(fd);
        return 1;
    }
    
    // Write back modified inodes
    for (int i = 0; i < 5; i++) {
        if (write_block(INODE_TABLE_START + i, &inodes[i * (BLOCK_SIZE / INODE_SIZE)]) < 0) {
            perror("Failed to write inode table");
            close(fd);
            return 1;
        }
    }
    
    // Re-check file system
    printf("\nRe-checking file system after fixes...\n");
    errors = 0;
    
    // Re-read superblock
    if (read_block(0, &sb) < 0) {
        perror("Failed to re-read superblock");
        close(fd);
        return 1;
    }
    errors += validate_superblock(&sb);
    
    // Re-read bitmaps
    if (read_block(INODE_BITMAP_BLOCK, inode_bitmap) < 0 ||
        read_block(DATA_BITMAP_BLOCK, data_bitmap) < 0) {
        perror("Failed to re-read bitmaps");
        close(fd);
        return 1;
    }
    
    // Re-read inode table
    for (int i = 0; i < 5; i++) {
        if (read_block(INODE_TABLE_START + i, &inodes[i * (BLOCK_SIZE / INODE_SIZE)]) < 0) {
            perror("Failed to re-read inode table");
            close(fd);
            return 1;
        }
    }
    
    // Re-run checks
    errors += check_bitmaps();
    errors += check_duplicates();
    errors += check_bad_blocks();
    
    printf("\nTotal errors found initially: %d\n", errors + fixes);
    printf("Total fixes applied: %d\n", fixes);
    printf("Total errors after fixes: %d\n", errors);
    
    close(fd);
    return errors > 0 ? 1 : 0;
}