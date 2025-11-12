#include <dirent.h> 
#include <stdio.h> 
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <pthread.h>

#include "serial.h"

#define BUFFER_SIZE 1048576 // 1MB
#define MAX_WORKER_THREADS 6

int cmp(const void *a, const void *b) {
	return strcmp(*(char **) a, *(char **) b);
}

// Worker thread function that processes files from the work queue
void* worker_thread(void* arg) {
	ThreadArgs *args = (ThreadArgs*)arg;
	
	// Use thread's pre-allocated buffers
	unsigned char *buffer_in = args->buffer_in;
	unsigned char *buffer_out = args->buffer_out;
	z_stream *strm = args->strm;
	
	while (1) {
		int file_index;
		
		// Lock mutex to safely get next file index
		pthread_mutex_lock(&args->queue->mutex);
		if (args->queue->next_index >= args->queue->total_files) {
			// No more work
			pthread_mutex_unlock(&args->queue->mutex);
			break;
		}
		file_index = args->queue->next_index;
		args->queue->next_index++;
		pthread_mutex_unlock(&args->queue->mutex);
		
		// Build full file path
		char *filename = args->queue->files[file_index];
		int len = strlen(args->directory_name) + strlen(filename) + 2;
		char *full_path = malloc(len * sizeof(char));
		assert(full_path != NULL);
		strcpy(full_path, args->directory_name);
		strcat(full_path, "/");
		strcat(full_path, filename);
		
		// Read file using pre-allocated buffer
		FILE *f_in = fopen(full_path, "r");
		assert(f_in != NULL);
		int nbytes = fread(buffer_in, sizeof(unsigned char), BUFFER_SIZE, f_in);
		fclose(f_in);
		
		// Compress file using pre-initialized zlib stream
		deflateReset(strm);        // Reset the stream for reuse
		strm->avail_in = nbytes;
		strm->next_in = buffer_in;
		strm->avail_out = BUFFER_SIZE;
		strm->next_out = buffer_out;
		
		int ret = deflate(strm, Z_FINISH);
		assert(ret == Z_STREAM_END);
		
		int nbytes_zipped = BUFFER_SIZE - strm->avail_out;
		
		// Store result in results array
		args->results[file_index].data = malloc(nbytes_zipped * sizeof(unsigned char));
		assert(args->results[file_index].data != NULL);
		memcpy(args->results[file_index].data, buffer_out, nbytes_zipped);
		args->results[file_index].size = nbytes_zipped;
		args->results[file_index].original_size = nbytes;
		
		// Update statistics atomically
		pthread_mutex_lock(args->stats_mutex);
		*(args->total_in) += nbytes;
		*(args->total_out) += nbytes_zipped;
		pthread_mutex_unlock(args->stats_mutex);
		
		// Cleanup - don't free buffers, they're reused
		free(full_path);
	}
	
	return NULL;
}

int compress_directory(char *directory_name) {
	DIR *d;
	struct dirent *dir;
	char **files = NULL;
	int nfiles = 0;

	d = opendir(directory_name);
	if(d == NULL) {
		printf("An error has occurred\n");
		return 0;
	}

	// Create sorted list of text files
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

	// Allocate results array
	CompressedResult *results = malloc(nfiles * sizeof(CompressedResult));
	assert(results != NULL);
	
	// Initialize work queue
	WorkQueue queue;
	queue.files = files;
	queue.total_files = nfiles;
	queue.next_index = 0;
	pthread_mutex_init(&queue.mutex, NULL);
	
	// Initialize statistics
	int total_in = 0, total_out = 0;
	pthread_mutex_t stats_mutex;
	pthread_mutex_init(&stats_mutex, NULL);
	
	// Determine number of worker threads
	int num_threads = MAX_WORKER_THREADS;
	if (nfiles < num_threads) {
		num_threads = nfiles;
	}

	// Create pthread-specific data for each thread
	typedef struct {
		ThreadArgs args;
		unsigned char buffer_in[BUFFER_SIZE];
		unsigned char buffer_out[BUFFER_SIZE];
		z_stream strm;
	} ThreadData;
	
	ThreadData *thread_data = malloc(num_threads * sizeof(ThreadData));
	assert(thread_data != NULL);
	
	// Set up each thread's data
	for (int i = 0; i < num_threads; i++) {
		thread_data[i].args.queue = &queue;
		thread_data[i].args.results = results;
		thread_data[i].args.directory_name = directory_name;
		thread_data[i].args.total_in = &total_in;
		thread_data[i].args.total_out = &total_out;
		thread_data[i].args.stats_mutex = &stats_mutex;
		thread_data[i].args.buffer_in = thread_data[i].buffer_in;
		thread_data[i].args.buffer_out = thread_data[i].buffer_out;
		thread_data[i].args.strm = &thread_data[i].strm;
		
		// Initialize zlib stream once per thread
		int ret = deflateInit(&thread_data[i].strm, 9);
		assert(ret == Z_OK);
	}
	
	// Create thread pool
	pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
	assert(threads != NULL);
	
	for (int i = 0; i < num_threads; i++) {
		int ret = pthread_create(&threads[i], NULL, worker_thread, &thread_data[i].args);
		assert(ret == 0);
	}
	
	// Wait for all threads to complete
	for (int i = 0; i < num_threads; i++) {
		pthread_join(threads[i], NULL);
	}

	// Clean up zlib resources
	for (int i = 0; i < num_threads; i++) {
		deflateEnd(&thread_data[i].strm);
	}
	free(thread_data);
	
	// Write results in order to output file
	FILE *f_out = fopen("text.tzip", "w");
	assert(f_out != NULL);
	
	for (int i = 0; i < nfiles; i++) {
		fwrite(&results[i].size, sizeof(int), 1, f_out);
		fwrite(results[i].data, sizeof(unsigned char), results[i].size, f_out);
		free(results[i].data);
	}
	fclose(f_out);

	printf("Compression rate: %.2lf%%\n", 100.0*(total_in-total_out)/total_in);

	// Cleanup
	pthread_mutex_destroy(&queue.mutex);
	pthread_mutex_destroy(&stats_mutex);
	free(threads);
	free(results);
	
	// Release list of files
	for(int i = 0; i < nfiles; i++)
		free(files[i]);
	free(files);

	return 0;
}
