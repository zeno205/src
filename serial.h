#ifndef SERIAL_H
#define SERIAL_H

/*
 * Project 2: Text ZIP Compression
 * Group: [Your Group Number/Name]
 * Authors: 
 *   - [Name1] ([NetID1])
 *   - [Name2] ([NetID2])
 *   - [Name3] ([NetID3])
 *   - [Name4] ([NetID4]) [if applicable]
 * 
 * Description: Parallel text file compression using pthreads.
 */

#include <pthread.h>
#include <zlib.h>

// Compressed result container
// Threads finish asynchronously, so results are buffered in memory
// for sequential writing by the main thread
typedef struct {
	unsigned char *data; // compressed data buffer
	int size; // compressed data length
	int orig_size; // original file size for stats
} CompResult;

// Shared list of tasks
// Acts as synchronized task distributor for worker threads
typedef struct {
	char **files; // array of file paths to process
	int total; // total number of files
	int next_idx; // index of next file to assign
	pthread_mutex_t lock; // synchronization lock for thread-safe access
} TaskQueue;

// Thread arguments
// Bundles shared resources and thread local buffers
typedef struct {
	TaskQueue *queue; // shared list of tasks
	CompResult *results; // shared results array
	char *dir_name; // base directory path

	// Global statistics counters (shared)
	int *total_in;
	int *total_out;
	pthread_mutex_t *stats_lock; // lock for updating stats

	// Thread-local buffers to avoid allocation overhead
	unsigned char *buf_in; // 1MB input buffer
	unsigned char *buf_out; // 1MB output buffer
	z_stream *strm; // zlib compression state
} WorkerArgs;

int compress_directory(char *directory_name);

#endif
