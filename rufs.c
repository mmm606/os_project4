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
int root_inode_number = 0;

// Declare your in-memory data structures here

//will claim next available bit from the inode bitmap if inode == true, 
// otherwise it will claim from the datablock bitmap
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

	int byte_num = 0;  
	int char_num = 0; 
	for(char_num = 0; char_num < limit / BITS_IN_BYTE; char_num++){
		for(byte_num = 0; byte_num < BITS_IN_BYTE; byte_num++){
			uint8_t val = get_bitmap(bitmap[char_num], byte_num);
			if(val == 0){
				break;	
			}
		}
	}
	
	// Step 3: Update bitmap and write to disk 

	set_bitmap(bitmap[char_num], byte_num);

	free(bitmap);

	int next_avail = (char_num * BITS_IN_BYTE) + byte_num;

	if(next_avail > limit){
		next_avail = -1;
	}

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

	// Step 3: Read the block from disk and read block from disk
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


/* 
 * directory operations
 */

// //Reads all direct entries of the current directory to see if the desired
// // file or sub-directory exists. If it exists, then put it into struct dirent dirent*
// int dir_find_or_add(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent, bool find) {

// 	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
// 	struct inode inode;
// 	readi(ino, &inode);

// 	// Step 2: Get data block of current directory from inode

// 	// Step 3: Read directory's data block and check each directory entry.
// 	//If the name matches, then copy directory entry to dirent structure

// 	struct dirent curr_dir;

// 	int direct_pointer_num;
// 	for(direct_pointer_num = 0; direct_pointer_num < NUM_DIRECT_POINTERS_PER_INODE; direct_pointer_num++){
// 		void *block_of_mem = malloc(BLOCK_SIZE);	
// 		bio_read(inode.direct_ptr[direct_pointer_num], block_of_mem);
// 		curr_dir = ((struct dirent *) block_of_mem)[0];
// 		free(block_of_mem);
// 		if(curr_dir.valid && strcmp(curr_dir.name, fname) == 0){
// 			break;
// 		}
// 	}

// 	if(find){
// 		if(direct_pointer_num == NUM_DIRECT_POINTERS_PER_INODE){
// 			return -1;
// 		}

// 		memcpy(dirent, &curr_dir, sizeof(struct dirent));
// 	}
// 	else{
// 		if(direct_pointer_num != NUM_DIRECT_POINTERS_PER_INODE){
// 			return -1;
// 		}

// 	}

// 	return 0;
// }

//Reads all direct entries of the current directory to see if the desired
// file or sub-directory exists. If it exists, then put it into struct dirent dirent*
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	struct inode inode;
	readi(ino, &inode);

	// Step 2: Get data block of current directory from inode

	// Step 3: Read directory's data block and check each directory entry.
	//If the name matches, then copy directory entry to dirent structure

	struct dirent curr_dir;
	int direct_pointer_num;
	for(direct_pointer_num = 0; direct_pointer_num < NUM_DIRECT_POINTERS_PER_INODE; direct_pointer_num++){
		void *block_of_mem = malloc(BLOCK_SIZE);	
		bio_read(inode.direct_ptr[direct_pointer_num], block_of_mem);
		
		curr_dir = ((struct dirent *) block_of_mem)[0];
		free(block_of_mem);

		if(curr_dir.valid && strcmp(curr_dir.name, fname) == 0){
			break;
		}
	}

	if(direct_pointer_num == NUM_DIRECT_POINTERS_PER_INODE){
		return -1;
	}

	memcpy(dirent, &curr_dir, sizeof(struct dirent));
	return 0;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	int open_index = -1;
	
	// Step 2: Check if fname (directory name) is already used in other entries
	for(int direct_pointer_num = 0; direct_pointer_num < NUM_DIRECT_POINTERS_PER_INODE; direct_pointer_num++){
		void *block_of_mem = malloc(BLOCK_SIZE);	
		bio_read(dir_inode.direct_ptr[direct_pointer_num], block_of_mem);
		
		struct dirent curr_dir = ((struct dirent *) block_of_mem)[0];
		free(block_of_mem);

		if(!curr_dir.valid && open_index == -1){
			open_index = direct_pointer_num;
		}
		elif(curr_dir.valid && strcmp(curr_dir.name, fname) == 0){
			return -1;
		}
	}

	if(open_index == -1){
		return -1;
	}

	// Step 3: Add directory entry in dir_inode's data block and write to disk

	// Allocate a new data block for this directory if it does not exist
	int avail_blockno = get_avail_blkno();
	if(avail_blockno == -1){
		return -1;
	}

	// Update directory inode
	dir_inode.direct_ptr[open_index] = avail_blockno;

	// Write directory entry
	void *memblock = malloc(BLOCK_SIZE);
	bio_read(avail_blockno, memblock);

	struct dirent new_dirent{.ino = f_ino, .valid = true, .name = fname, .len = strlen(fname)};
	memcpy(memblock, new_dirent, sizeof(struct dirent));

	bio_write(avail_blockno, memblock);
	free(memblock);

	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return 0;
}

// char **split(char *strin, int start, int range char delim){
// 	for(int i = start; i < start + range; i++){
// 		split
// 	}
// }

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way
	if(path[0] != '/'){
		return -1;
	}

	uint16_t current_parent_inode = ino;

	for(char *curr_token = strtok(path, '/'); curr_token != NULL; curr_token = strtok(NULL, '/')){
		struct dirent next_dirent;

		dir_find(current_parent_inode, curr_token, strlen(curr_token), &next_dirent);

		current_parent_inode = next_dirent.ino;

	}
	readi(current_parent_inode, inode);
	return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {

	void *block_mem = calloc(1, BLOCK_SIZE);

	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);

	// write superblock information
	bio_read(0, block_mem);
	superblock = *((struct superblock *) block_mem);

	memset(block_mem, 0, BLOCK_SIZE);

	// initialize inode bitmap
	bio_write(superblock.i_bitmap_blk, block_mem);
	for(int i = superblock.i_start_blk; i < MAX_INUM; i++){
		bio_write(i, block_mem);
	}

	// initialize data block bitmap
	bio_write(superblock.d_bitmap_blk, block_mem);
	for(int i = superblock.d_start_blk; i < MAX_DNUM; i++){
		bio_write(i, block_mem);
	}

	// update bitmap information for root directory
	int root_data_block_no = get_avail_blkno();
	root_inode_number = get_avail_blkno();

	// update inode for root directory
	// Here I am creating the inode that points to the data block
	struct inode root_dirs_inode;
	memset(&root_dirs_inode, 0, sizeof(struct inode));
	root_dirs_inode.ino = root_inode_number;
	root_dirs_inode.valid = true;
	root_dirs_inode.size = sizeof(struct dirent);
	root_dirs_inode.link = 1;
	root_dirs_inode.type = 0;
	root_dirs_inode.direct_ptr[0] = root_data_block_no;
	writei(root_inode_number, &root_dirs_inode);

	// Here I am creating the root directory pointed to by the inode
	char *root_name = "/";
	struct dirent new_dir{.ino = root_inode_number, .valid = true, .name = root_name, strlen(root_name)};

	memcpy(block_mem, &new_dir, sizeof(struct dirent));

	bio_write(root_inode_number, block_mem);

	free(block_mem);
	return 0;
}


/* 
 * FUSE file operations
*/
//TODO: I always just calls mkfs, not sure what to do otherwise, especially with this param
static void *rufs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs

	// Step 1b: If disk file is found, just initialize in-memory data structures
	// and read superblock from disk
	rufs_mkfs();
	return NULL;
}

static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures

	// Step 2: Close diskfile
	dev_close();
}

//TODO: NOT REALLY SURE WHAT THIS IS SUPPOSED TO DO
static int rufs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path
	struct inode inode;
	get_node_by_path(path, root_inode_number, inode);

	// Step 2: fill attribute of file into stbuf from inode
	// inode.
	stbuf.
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

