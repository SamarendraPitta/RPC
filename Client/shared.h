#ifndef SHARED_H
#define SHARED_H

#include "replicator.h"
#include <rpc/rpc.h>
#include <pthread.h>

#define MAX_SERVERS 5
#define MAX_JOBS 1024
#define CPU_HIGH_THRESHOLD 80
#define CPU_LOW_THRESHOLD 20

typedef enum
{
    JOB_PENDING,
    JOB_RUNNING,
    JOB_COMPLETED,
    JOB_FAILED
} job_status_t;

typedef struct
{
    int job_id;
    int assigned_server;
    job_status_t status;
} job_t;

typedef struct
{
    char host[128];
    CLIENT *clnt;
    int active;
    float cpu_load;
} ServerInfo;

extern ServerInfo servers[MAX_SERVERS]; // Use ServerInfo consistently
extern int num_servers;

extern job_t jobs[MAX_JOBS];
extern int total_jobs;

void read_hosts();
void start_specific_server(char *);
void stop_specific_server(char *);
void print_cpu_load(char *);
void run_job(int start, int end, int step);

#endif // SHARED_H
