/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];

struct superblock superblock;

// Declare your in-memory data structures here
int claim_next_avail_bit(bool inode) {

	bitmap_t *bitmap = malloc(sizeof(BLOCK_SIZE));

	// Step 1: Read bitmap from disk
	int block_num = superblock.i_bitmap_blk;
	size_t limit = MAX_INUM;
	if(!inode){
		block_num = superblock.d_bitmap_blk;
		limit = MAX_DNUM;
	}
	//TODO: make sure we read how much we need and don't read garbage maybe make it calloc
	bio_read(block_num, bitmap);
	
	// Step 2: Traverse bitmap to find an available slot

	int bit_num = 0;  
	int char_num = 0; 
	for(char_num = 0; char_num < limit / BITS_IN_BYTE; char_num++){
		for(bit_num = 0; bit_num < BITS_IN_BYTE; bit_num++){
			uint8_t val = get_bitmap(bitmap[char_num], bit_num);
			if(val == 0){
				break;	
			}
		}
	}
	
	// Step 3: Update bitmap and write to disk 

	int next_avail = (char_num * BITS_IN_BYTE) + bit_num;

	if(next_avail > limit){
		return -1;
	}

	set_bitmap(bitmap[char_num], bit_num);
	bio_write(block_num, bitmap);

	free(bitmap);

	return next_avail;
}


/* 
 * Get available inode number from bitmap
*/
int get_avail_ino() {
	return claim_next_avail_bit(true);
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
	return claim_next_avail_bit(false);	
}
/* 
 * inode operations
*/
//will read if read == true, will write otherwise
int read_or_write_i(uint16_t ino, struct inode *inode, bool read) {

	// Step 1: Get the inode's on-disk block number
	int whole_block_number = superblock.i_bitmap_blk + (ino / sizeof(struct inode));

	// Step 2: Get offset of the inode in the inode on-disk block
	int intra_block_offet = (ino % sizeof(struct inode));

	// Step 3: Read the block from disk
	//	if read == true just copy from the disk to the inode struct
	//	otherwise augment what the block looks like and write it back

	struct inode* inode_block_buffer = malloc(sizeof(BLOCK_SIZE));
	bio_read(whole_block_number, inode_block_buffer);

	if(read){
		memcpy(inode, inode_block_buffer + intra_block_offet, sizeof(struct inode));
	}
	else{
		memcpy(inode_block_buffer + intra_block_offet, inode, sizeof(struct inode));
		bio_write(whole_block_number, inode_block_buffer);
	}
	
	free(inode_block_buffer);

	return 0;
}

int readi(uint16_t ino, struct inode *inode) {
	return read_or_write_i(ino, inode, true);
}

int writei(uint16_t ino, struct inode *inode) {
	return read_or_write_i(ino, inode, false);
}



//return true if found, false otherwise, populates given dirent struct if it isn't null
bool search_block_of_dir_entries(int block_number, char *fname, struct dirent *found_directory_entry){
	struct dirent *block_of_dir_entries = malloc(BLOCK_SIZE);
	bio_read(block_number, block_of_dir_entries);

	int i = 0;
	for(i = 0; i < BLOCK_SIZE; i += sizeof(struct dirent)){
		struct dirent curr_dir_entry = block_of_dir_entries[i];
		if(curr_dir_entry.valid && strcmp(curr_dir_entry.name, fname) == 0){
			memcpy(found_directory_entry, &curr_dir_entry, sizeof(struct dirent));
			break;
		}
	}

	free(block_of_dir_entries);

	if(i < BLOCK_SIZE){
		return true;
	}

	memset(found_directory_entry, 0, sizeof(struct dirent));
	return false;
}		

bool search_block_number_containing_dir_entries(int block_number){

}

//return true if found, false otherwise, populates given dirent struct if it isn't null
bool search_dir_given_inode(struct inode dir_inode, char * fname, struct dirent *found_directory_entry){

	int i = 0;
	for(i = 0; i < DIRECT_POINTERS_PER_INODE; i ++){

		if(search_block_of_dir_entries(dir_inode.direct_ptr[i], fname, found_directory_entry)){
			break;
		}
	}

	if(i < DIRECT_POINTERS_PER_INODE){
		return true;
	}

	for(i = 0; i < INDIRECT_POINTERS_PER_INODE; i++){
		int *block_number_array = malloc(BLOCK_SIZE);
		bio_read(dir_inode.indirect_ptr[i], block_number_array);

		int index_into_block_number_array = 0;
		for(index_into_block_number_array = 0; index_into_block_number_array < BLOCK_SIZE / sizeof(int); i++){
			if(search_block_of_dir_entries(block_number_array[index_into_block_number_array], fname, found_directory_entry)){
				break;
			}
		}
		free(block_number_array);
		if(index_into_block_number_array < BLOCK_SIZE / sizeof(int)){
			return true;
		}
	}

	return false;
}

/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

  // Step 1: Call readi() to get the inode using ino (inode number of current directory)
	struct inode inode;
	readi(ino, &inode);

	// Step 2: Get data block of current directory from inode


	// Step 3: Read directory's data block and check each directory entry.
	//If the name matches, then copy directory entry to dirent structure
	if(search_dir_given_inode(inode, fname, dirent)){
		return 0;
	}
	return -1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode

	
	// Step 2: Check if fname (directory name) is already used in other entries

	// Step 3: Add directory entry in dir_inode's data block and write to disk

	// Allocate a new data block for this directory if it does not exist

	// Update directory inode

	// Write directory entry

	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	

	return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile

	// write superblock information

	// initialize inode bitmap

	// initialize data block bitmap

	// update bitmap information for root directory

	// update inode for root directory

	return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs

  // Step 1b: If disk file is found, just initialize in-memory data structures
  // and read superblock from disk

	return NULL;
}

static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures

	// Step 2: Close diskfile

}

static int rufs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path

	// Step 2: fill attribute of file into stbuf from inode

		stbuf->st_mode   = S_IFDIR | 0755;
		stbuf->st_nlink  = 2;
		time(&stbuf->st_mtime);

	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

    return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: Read directory entries from its data blocks, and copy them to filler

	return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory

	// Step 5: Update inode for target directory

	// Step 6: Call writei() to write inode to disk
	

	return 0;
}

static int rufs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of target directory

	// Step 3: Clear data block bitmap of target directory

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

	return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target file to parent directory

	// Step 5: Update inode for target file

	// Step 6: Call writei() to write inode to disk

	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

	return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	return 0;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk
	return size;
}

static int rufs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of target file

	// Step 3: Clear data block bitmap of target file

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

	return 0;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}

