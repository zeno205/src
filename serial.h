#ifndef SERIAL_H
#define SERIAL_H

/*
 * Project 2: Text ZIP Compression
 * Group: 20
 * Authors: 
 * Rice Pham U11328727
 * Harry Pham U60334857
 * Jolie Nguyen U71766734
 * Veera Saideep Sasank Vulavakayala U83668312
 * 
 * Description: Parallel text compression tool that scans a directory for input files and compresses them into a single archive. It uses a shared task queue and multiple pthread worker threads so files can be processed at the same time. Each thread compresses data on its own and results are written out in the same order every time
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
