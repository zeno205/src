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

#include <dirent.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <pthread.h>

#include "serial.h"

#define BUFFER_SIZE 1048576 // 1MB
#define MAX_WORKER_THREADS 8 // optimal for 4-core system

// String comparator for qsort
int cmp(const void *a, const void *b) {
	return strcmp(*(char **) a, *(char **) b);
}

// Worker thread function
// Processes files from the shared queue until exhausted
void* worker_thread(void* arg) {
	WorkerArgs *args = (WorkerArgs*)arg;

	// Cache buffer pointers locally for faster access
	// These buffers were allocated once per thread at startup and will be reused for all files this thread processes to avoid expensive malloc/free in the loop
	unsigned char *buf_in = args->buf_in;
	unsigned char *buf_out = args->buf_out;
	z_stream *strm = args->strm;

	while (1) {
		int idx;

		// Critical section: atomically fetch next file index
		pthread_mutex_lock(&args->queue->lock);

		if (args->queue->next_idx >= args->queue->total) {
			pthread_mutex_unlock(&args->queue->lock);
			break;
		}

		idx = args->queue->next_idx;
		args->queue->next_idx++;

		pthread_mutex_unlock(&args->queue->lock);

		// Build full file path
		char *filename = args->queue->files[idx];
		int len = strlen(args->dir_name) + strlen(filename) + 2;
		char *path = malloc(len * sizeof(char));
		assert(path != NULL);
		strcpy(path, args->dir_name);
		strcat(path, "/");
		strcat(path, filename);

		// Read file into buffer
		FILE *f_in = fopen(path, "r");
		assert(f_in != NULL);
		int nbytes = fread(buf_in, sizeof(unsigned char), BUFFER_SIZE, f_in);
		fclose(f_in);

		// Compress file data
		deflateReset(strm);
		strm->avail_in = nbytes;
		strm->next_in = buf_in;
		strm->avail_out = BUFFER_SIZE;
		strm->next_out = buf_out;

		int ret = deflate(strm, Z_FINISH);
		assert(ret == Z_STREAM_END);

		int nbytes_zipped = BUFFER_SIZE - strm->avail_out;

		// Store compressed result in order-preserving array
		args->results[idx].data = malloc(nbytes_zipped * sizeof(unsigned char));
		assert(args->results[idx].data != NULL);
		memcpy(args->results[idx].data, buf_out, nbytes_zipped);
		args->results[idx].size = nbytes_zipped;
		args->results[idx].orig_size = nbytes;

		// Critical section: update global statistics
		pthread_mutex_lock(args->stats_lock);
		*(args->total_in) += nbytes;
		*(args->total_out) += nbytes_zipped;
		pthread_mutex_unlock(args->stats_lock);

		free(path);
	}

	return NULL;
}

int compress_directory(char *dir_name) {
	DIR *d;
	struct dirent *dir;
	char **files = NULL;
	int nfiles = 0;

	// Scan directory for .txt files
	d = opendir(dir_name);
	if(d == NULL) {
		printf("An error has occurred\n");
		return 0;
	}

	while ((dir = readdir(d)) != NULL) {
		files = realloc(files, (nfiles+1)*sizeof(char *));
		assert(files != NULL);

		int len = strlen(dir->d_name);
		if(len >= 4 && dir->d_name[len-4] == '.' && dir->d_name[len-3] == 't' && dir->d_name[len-2] == 'x' && dir->d_name[len-1] == 't') {
			files[nfiles] = strdup(dir->d_name);
			assert(files[nfiles] != NULL);
			nfiles++;
		}
	}
	closedir(d);
	qsort(files, nfiles, sizeof(char *), cmp);

	// Allocate results array for all files
	CompResult *results = malloc(nfiles * sizeof(CompResult));
	assert(results != NULL);

	// Initialize shared list of tasks
	TaskQueue queue;
	queue.files = files;
	queue.total = nfiles;
	queue.next_idx = 0;
	pthread_mutex_init(&queue.lock, NULL);

	// Initialize shared statistics
	int total_in = 0, total_out = 0;
	pthread_mutex_t stats_lock;
	pthread_mutex_init(&stats_lock, NULL);

	// Determine optimal thread count
	int num_threads = MAX_WORKER_THREADS;
	if (nfiles < num_threads) {
		num_threads = nfiles;
	}

	// Thread data structure with embedded buffers
	typedef struct {
		WorkerArgs args;
		unsigned char buf_in[BUFFER_SIZE];
		unsigned char buf_out[BUFFER_SIZE];
		z_stream strm;
	} WorkerData;

	WorkerData *wdata = malloc(num_threads * sizeof(WorkerData));
	assert(wdata != NULL);

	// Configure each thread with shared resources and private buffers
	for (int i = 0; i < num_threads; i++) {
		wdata[i].args.queue = &queue;
		wdata[i].args.results = results;
		wdata[i].args.dir_name = dir_name;
		wdata[i].args.total_in = &total_in;
		wdata[i].args.total_out = &total_out;
		wdata[i].args.stats_lock = &stats_lock;

		wdata[i].args.buf_in = wdata[i].buf_in;
		wdata[i].args.buf_out = wdata[i].buf_out;
		wdata[i].args.strm = &wdata[i].strm;

		int ret = deflateInit(&wdata[i].strm, 9);
		assert(ret == Z_OK);
	}

	// Spawn worker threads
	pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
	assert(threads != NULL);

	for (int i = 0; i < num_threads; i++) {
		int ret = pthread_create(&threads[i], NULL, worker_thread, &wdata[i].args);
		assert(ret == 0);
	}

	// Wait for all threads to complete
	for (int i = 0; i < num_threads; i++) {
		pthread_join(threads[i], NULL);
	}

	// Clean up zlib resources
	for (int i = 0; i < num_threads; i++) {
		deflateEnd(&wdata[i].strm);
	}
	free(wdata);

	// Write results sequentially to preserve order
	FILE *f_out = fopen("text.tzip", "w");
	assert(f_out != NULL);

	for (int i = 0; i < nfiles; i++) {
		fwrite(&results[i].size, sizeof(int), 1, f_out);
		fwrite(results[i].data, sizeof(unsigned char), results[i].size, f_out);
		free(results[i].data);
	}
	fclose(f_out);

	printf("Compression rate: %.2lf%%\n", 100.0*(total_in-total_out)/total_in);

	// Final cleanup
	pthread_mutex_destroy(&queue.lock);
	pthread_mutex_destroy(&stats_lock);

	free(threads);
	free(results);

	for(int i = 0; i < nfiles; i++)
		free(files[i]);
	free(files);

	return 0;
}
