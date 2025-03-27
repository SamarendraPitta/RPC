#include "replicator.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>

#define MAXIMUM_RUNNING_JOBS_COUNT 100

static int Total_Job_Count = 0;
static pid_t running_jobs[MAXIMUM_RUNNING_JOBS_COUNT];
static int Server_Active_status = 1;
pthread_mutex_t Mutex_for_jobs = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t jobCond = PTHREAD_COND_INITIALIZER;

// below function handles to start the server by client commands..
// Req_Pointer specifes the, procedure number, version number and client credentials.
// based on that server executes and gives  it to server stub.
// server stub will make a make a call to client stub and client stub gives it to user
int *start_server_1_svc(void *argp, struct svc_req *Req_Pointer)
{
    static int Result;

    // checking server status,
    if (!Server_Active_status)
    {
        Server_Active_status = 1;
        printf("Server started.\n");
        Result = 0;
    }
    else
    {
        printf("Server already active.\n");
        Result = -1;
    }
    return &Result;
}

// below functions executes the jobs assigned to the server.
// by creating the child process and
int *run_job_1_svc(job_args *Argumnts, struct svc_req *Req_Pointer)
{
    static int result;

    // checking server status, if it is inactive sending message to user.
    if (!Server_Active_status)
    {
        fprintf(stderr, "Server is inactive and Rejecting jobs.\n");
        result = -1;
        return &result;
    }

    // creating the child process using fork()
    pid_t Process_ID = fork();
    if (Process_ID == 0)
    {
        mkdir("replicate_out", 0755);
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "./hyper_link %d 1000000 999 1 2 2 50 > replicate_out/job_%d.txt", Argumnts->job_id, Argumnts->job_id);
        int Ret_val = system(cmd);
        if (Ret_val != 0)
            fprintf(stderr, "Job %d failed due to server stop or high threshhold\n", Argumnts->job_id);
        exit(0);
    }
    // if job is executed then job count is added and specifies to user that job completed successfully.
    else if (Process_ID > 0)
    {
        pthread_mutex_lock(&Mutex_for_jobs);
        running_jobs[Total_Job_Count++] = Process_ID;
        pthread_mutex_unlock(&Mutex_for_jobs);
        printf("Job %d started with Process_ID %d\n", Argumnts->job_id, Process_ID);
        result = 0;
    }
    else
    {
        perror("Fork failed");
        result = -1;
    }
    return &result;
}

// below functions performs arithmetic operartions that is addition, subtraction, mul, and squaring.
int *subtract_1_svc(operands *Argumnts, struct svc_req *Req_Pointer)
{
    static int Operation_Res;
    Operation_Res = Argumnts->a - Argumnts->b;
    return &Operation_Res;
}

int *multiply_1_svc(operands *Argumnts, struct svc_req *Req_Pointer)
{
    static int Operation_Res;
    Operation_Res = Argumnts->a * Argumnts->b;
    return &Operation_Res;
}

// when client stub requests for cpu load, the server stub will receive and hand over to server.
// here server will execute, i mean fetches the cpu load.
// using pro/load/avg
cpu_load *get_cpu_load_1_svc(void *argp, struct svc_req *Req_Pointer)
{
    static cpu_load load;
    FILE *file_pointer = fopen("/proc/loadavg", "r");
    if (file_pointer == NULL)
    {
        perror("error occured while opening file to get cpuload.\n");
    }
    if (fscanf(file_pointer, "%f", &load.load_avg) != 1)
        load.load_avg = -1.0;
    fclose(fopen("/proc/loadavg", "r"));
    return &load;
}

server_status *get_status_1_svc(void *a, struct svc_req *Req_Pointer)
{
    static server_status ss;
    ss.is_active = Server_Active_status;
    return &ss;
}

int *power_1_svc(operands *Argumnts, struct svc_req *Req_Pointer)
{
    static int Operation_Res = 1;
    for (int i = 0; i < Argumnts->b; i++)
        Operation_Res *= Argumnts->a;
    return &Operation_Res;
}

// below procedure is for stopping the specific server.
// when the server is exercuting the jobs i means when jobs are running it will kill all the jobs and server goes to inactive state.
int *stop_server_1_svc(void *argp, struct svc_req *Req_Pointer)
{
    static int result = 0;
    // if server is active then setting server active status to 0 and then killing jobs.
    if (Server_Active_status)
    {
        Server_Active_status = 0;

        // below statement is used to wake up all the threads waiting in the queue.
        pthread_cond_broadcast(&jobCond);

        // when server is stopped opening the hyper_link file and killing all the jobs using system() call.
        FILE *fp = popen("pgrep hyper_link", "r");
        if (fp)
        {
            char buffer[128];
            if (fgets(buffer, sizeof(buffer), fp) != NULL)
            {
                system("pkill -9 hyper_link");
            }
            pclose(fp);
        }
        else
        {
            perror("error occured");
        }

        printf("Server has been stopped successfully.\n");
    }
    else
    {
        printf("Server is already active.\n");
        result = -1;
    }

    return &result;
}

int *add_1_svc(operands *Argumnts, struct svc_req *Req_Pointer)
{
    static int Operation_Res;
    Operation_Res = Argumnts->a + Argumnts->b;
    return &Operation_Res;
}
