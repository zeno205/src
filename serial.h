#ifndef __SERIAL_H__
#define __SERIAL_H__

#include <pthread.h>

// Structure to hold compressed file result
typedef struct {
    unsigned char *data;      // Compressed data buffer
    int size;                 // Size of compressed data
    int original_size;        // Original uncompressed size
} CompressedResult;

// Work queue structure for thread-safe file distribution
typedef struct {
    char **files;             // Array of file paths
    int total_files;          // Total number of files
    int next_index;           // Next file index to process
    pthread_mutex_t mutex;    // Mutex for thread-safe access
} WorkQueue;

// Arguments passed to worker threads
typedef struct {
    WorkQueue *queue;
    CompressedResult *results;
    char *directory_name;
    int *total_in;
    int *total_out;
    pthread_mutex_t *stats_mutex;
} ThreadArgs;

int compress_directory(char *directory_name);

#endif
