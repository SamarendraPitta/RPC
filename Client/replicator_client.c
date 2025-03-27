#include "replicator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_RETRIES 3
#define RETRY_DELAY 3
#define MAXIMUM_SERVERS 5

// setting default values for cpu high and low threshold
float SET_CPU_HIGH_THRESHOLD = 80.0;
float SET_CPU_LOW_THRESHOLD = 20.0;

typedef struct
{
    char hostname[128];
    CLIENT *clnt;
    int Server_Active_Status;
    float cpu_load;
} SERVER_INFO;

typedef enum
{
    JOB_PENDING,
    JOB_RUNNING,
    JOB_COMPLETED,
    JOB_FAILED
} job_status_t;

typedef struct
{
    int job_Id;
    int Server_assigned;
    int Job_status;
} Job;

SERVER_INFO servers[MAXIMUM_SERVERS];
Job jobs[MAXIMUM_SERVERS * 100];
int NUM_OF_SERVERS = 0;
int Jobs_Total = 0;
int Jobs_Throttle = 0;

pthread_mutex_t Mutex_Server = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t Mutex_Job = PTHREAD_MUTEX_INITIALIZER;

typedef struct
{
    int start;
    int end;
    int step;
} JobArguments;

void *run_job_thread(void *arg);
void Read_Servers_by_Hostnames();
int Select_Least_Load_Server();
void *Monitor_Thread_CPU(void *arg);
void Stop_Server_By_Hostname(char *hostname);
void start_specific_server(char *hostname);
void Addition_Operation(int x, int y, CLIENT *Client);
void Subtraction_Operation(int x, int y, CLIENT *Client);
void Multiplication_Operation(int x, int y, CLIENT *Client);
void Exponentiation(int x, int y, CLIENT *Client);
void Show_Help_Command();

// below functions shows what user commands this shell accepts.
void Show_Help_Command()
{
    printf("Commands:\n");
    printf("\tstart <hostname> - Start specific server\n");
    printf("\tstop <hostname> - Stop specific server\n");
    printf("\tstatus - Show status of all servers\n");
    printf("\tcpuload - Show CPU load of all servers\n");
    printf("\tadd <x> <y> - Additrion operation\n");
    printf("\tsubtract <x> <y> - subtraction\n");
    printf("\tmultiply <x> <y> - Perform multiplication\n");
    printf("\tpower <x> <y> - Exponentiation\n");
    printf("\thyper_link <start> <end> <step> - Run jobs in the background\n");
    printf("\tset_highthresh <value> - set cpu high threshhold\n");
    printf("\tset_lowthresh <value> - set cpu low threshhold\n");
    printf("\texit - quit the shell\n");
    fflush(stdout);
}

// The below function will read all the hostnames to make the client connetion.
// hostnameas are specified in the file called replicate.hosts
// protocol used is UDP to make connection with the server.
void Read_Servers_by_Hostnames()
{
    FILE *file_pointer = fopen("replicate.hosts", "r");
    if (!file_pointer)
    {
        perror("error occured while opening the file replicate.hosts");
        exit(1);
    }

    // reading all the servers and make client connection with the server.
    // if the connection is established then server status is updated to active, else inactive.
    while (fscanf(file_pointer, "%127s", servers[NUM_OF_SERVERS].hostname) != EOF && NUM_OF_SERVERS < MAXIMUM_SERVERS)
    {
        servers[NUM_OF_SERVERS].clnt = clnt_create(servers[NUM_OF_SERVERS].hostname, REPLICATOR_PROG, REPLICATOR_VERS, "udp");
        // If the connection is not established then status of that particular server is set as inactive.
        // else set as activee.
        if (!servers[NUM_OF_SERVERS].clnt)
        {
            clnt_pcreateerror(servers[NUM_OF_SERVERS].hostname);
            servers[NUM_OF_SERVERS].Server_Active_Status = 0;
        }
        else
        {
            servers[NUM_OF_SERVERS].Server_Active_Status = 1;
        }
        NUM_OF_SERVERS++;
    }
    fclose(file_pointer);
}

// below function monitors cpu for every 5 seconds.
// if the cpu goes high i mean the cpu load is above high threshhold then overload count is incremented.
// And it will throttle the jobs to another servers which has low threshhold values.
void *Monitor_Thread_CPU(void *Job_ARGS)
{
    while (1)
    {
        int Over_Count = 0;

        // checks cpuload for every 5 seconds and throttle jobs.
        // by making a rpc call and fetches the cpuload and checks with threshold values.
        pthread_mutex_lock(&Mutex_Server);
        for (int i = 0; i < NUM_OF_SERVERS; i++)
        {
            cpu_load *load = get_cpu_load_1(NULL, servers[i].clnt);
            if (load)
            {
                servers[i].cpu_load = load->load_avg;
                if (servers[i].cpu_load > SET_CPU_HIGH_THRESHOLD)
                {
                    Over_Count++;
                }
            }
            else
            {
                perror("failed to get the cpuload");
            }
        }

        // mutex lock is established while updating the throttle jobs.
        pthread_mutex_unlock(&Mutex_Server);
        pthread_mutex_lock(&Mutex_Job);
        Jobs_Throttle = (Over_Count == NUM_OF_SERVERS);
        pthread_mutex_unlock(&Mutex_Job);
        sleep(2);
    }
    return NULL;
}

int Select_Least_Load_Server()
{
    float Cpu_Min_load = SET_CPU_HIGH_THRESHOLD + 1.0;
    int Selected_Server = -1;
    int i = 0;

    pthread_mutex_lock(&Mutex_Server);

    while (i < NUM_OF_SERVERS)
    {

        // functions fetches the cpuload by making rpc call.
        // so when the cpuload gets less than min threshhold then that particular server is restarted again.
        // and setted server status as active.
        if (servers[i].Server_Active_Status)
        {
            cpu_load *load = get_cpu_load_1(NULL, servers[i].clnt);
            if (load && load->load_avg >= 0.0)
            {
                servers[i].cpu_load = load->load_avg;
                if (servers[i].cpu_load < Cpu_Min_load)
                {
                    Cpu_Min_load = servers[i].cpu_load;
                    Selected_Server = i;
                }
            }
            else
            {
                servers[i].Server_Active_Status = 0;
            }
        }
        i++;
    }
    pthread_mutex_unlock(&Mutex_Server);

    return Selected_Server;
}

// The below function handles to stop the specifc server.
// When the user requests to stop the specific server then all the jobs running
// in that particular server will be killed and set the status of all jobs to job_pending state.
void Stop_Server_By_Hostname(char *hostname)
{
    pthread_mutex_lock(&Mutex_Server);
    int i = 0;
    while (i < NUM_OF_SERVERS)
    {

        // Checking the server active status
        if (strcmp(servers[i].hostname, hostname) == 0 && servers[i].Server_Active_Status)
        {
            // the below function is remote procedure.
            // The logic for stopping the server is handled in server side.
            // client stub will make the RPC call to server to execute the remote procedure like normal call.
            int *result_stat = stop_server_1(NULL, servers[i].clnt);
            if (!result_stat || *result_stat != 0)
            {
                fprintf(stderr, "Failed to stop server %s.\n", hostname);
                fflush(stderr);
            }
            else
            {
                printf("Server %s stopped successfully.\n", hostname);
                // fflush(stdout);
                servers[i].Server_Active_Status = 0;

                // setting a mutex lock and if jobs are running setting as pending.
                pthread_mutex_lock(&Mutex_Job);
                for (int j = 0; j < Jobs_Total; j++)
                {
                    if (jobs[j].Job_status == JOB_RUNNING && jobs[j].Server_assigned == i)
                    {
                        // printf("Job %d killed due to server stop (%s). Reassigning...\n", jobs[j].job_Id, hostname);
                        // fflush(stdout);
                        jobs[j].Server_assigned = -1;
                        jobs[j].Job_status = JOB_PENDING;
                    }
                }
                pthread_mutex_unlock(&Mutex_Job);
            }
            break;
        }
        i++;
    }
    pthread_mutex_unlock(&Mutex_Server);
}

// The below handles to start the specific server
void start_specific_server(char *hostname)
{
    pthread_mutex_lock(&Mutex_Server);
    for (int i = 0; i < NUM_OF_SERVERS; i++)
    {
        // checking the server active status.
        // if not then client stub make a connection with server using rpc call start_server_1 procedure.
        if (!servers[i].Server_Active_Status && strcmp(servers[i].hostname, hostname) == 0)
        {
            int *result_stat = start_server_1(NULL, servers[i].clnt);
            if (!result_stat || *result_stat != 0)
            {
                fprintf(stderr, "Failed to start server %s.\n", hostname);
                // fflush(stderr);
            }
            else
            {
                printf("Server %s is started successfully.\n", hostname);
                // fflush(stdout);
                servers[i].Server_Active_Status = 1;
            }
            break;
        }
        // else{
        //	printf("server is already active");
        // }
    }
    pthread_mutex_unlock(&Mutex_Server);
}

// The below function handles the jobs running implemented using pthreads.
// I have round robin scheduling algorithms to schedule all jobs to all servers equally.
// Then after assigning the server starts doing the jobs.
// When the server is cancelled then jobs are killed and keep server status as inactive.
void *run_job_thread(void *arg)
{
    // fetching the parameter values from then job arguments.
    JobArguments *Job_ARGS = (JobArguments *)arg;
    int start = Job_ARGS->start;
    int end = Job_ARGS->end;
    int step = Job_ARGS->step;
    free(Job_ARGS);

    pthread_mutex_lock(&Mutex_Job);
    Jobs_Total = 0;

    for (int job_Id = start; job_Id <= end; job_Id += step)
    {
        jobs[Jobs_Total].job_Id = job_Id;
        jobs[Jobs_Total].Server_assigned = -1;
        jobs[Jobs_Total].Job_status = JOB_PENDING;
        Jobs_Total++;
    }
    pthread_mutex_unlock(&Mutex_Job);

    int completed_jobs = 0;
    int Current_Server_Index = 0;

    // untill all the jobs completed the below code will run conrtinously.
    while (completed_jobs < Jobs_Total)
    {
        pthread_mutex_lock(&Mutex_Job);
        if (Jobs_Throttle)
        {
            printf("All servers are overloaded. Throttling jobs...\n");
            // fflush(stdout);
            pthread_mutex_unlock(&Mutex_Job);
            sleep(5);
            continue;
        }
        pthread_mutex_unlock(&Mutex_Job);

        int i = 0;
        while (i < Jobs_Total)
        {

            // if job status is pending or failed then the below piece of code will try to allocate the jobs to the server.
            // I have taken the max retries as 3, so after 3 maximum retires the job will be cancelled automatically.
            if (jobs[i].Job_status == JOB_PENDING || jobs[i].Job_status == JOB_FAILED)
            {
                int Retry = 0;
                while (Retry < MAX_RETRIES)
                {
                    pthread_mutex_lock(&Mutex_Server);
                    int Server_Index = -1;
                    for (int j = 0; j < NUM_OF_SERVERS; j++)
                    {
                        Current_Server_Index = (Current_Server_Index + 1) % NUM_OF_SERVERS;
                        if (servers[Current_Server_Index].Server_Active_Status && servers[Current_Server_Index].cpu_load <= SET_CPU_HIGH_THRESHOLD)
                        {
                            Server_Index = Current_Server_Index;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&Mutex_Server);

                    // if server_index is -1 then there are no active servers and retries count is incremented and again retries to schedule job to active server.
                    if (Server_Index == -1)
                    {
                        // fprintf(stderr, "for Job %d. no server is active...retyring\n", jobs[i].job_Id);
                        sleep(RETRY_DELAY);
                        Retry++;
                        continue;
                    }

                    // making the rpc call to run the job.
                    // if the job is successfull then the status of job is set as comleted
                    job_args jargs = {jobs[i].job_Id};
                    int *result_stat = run_job_1(&jargs, servers[Server_Index].clnt);

                    // if result is not null then status of job is set as job running.
                    // else it is failed and it will retried again upto max reties count.
                    if (!result_stat || *result_stat != 0)
                    {
                        // fprintf(stderr, "job id %d failed on the server %s. retrying..\n", jobs[i].job_Id, servers[Server_Index].hostname);
                        Retry++;
                        sleep(RETRY_DELAY);
                    }
                    else
                    {
                        // printf("job id %d started successfully on the server with hostname %s.\n", jobs[i].job_Id, servers[Server_Index].hostname);
                        jobs[i].Job_status = JOB_RUNNING;
                        jobs[i].Server_assigned = Server_Index;
                        break;
                    }
                }
                // after max retires status of job is marked as failed.
                if (Retry == MAX_RETRIES)
                {
                    // fprintf(stderr, "Job %d failed after maximum retries.\n", jobs[i].job_Id);
                    // fflush(stderr);
                    jobs[i].Job_status = JOB_FAILED;
                }
            }
            // if the job is running then the status for job id is amrked as completed after 5 seconds.
            // and incrementing the completed jobs count.
            else if (jobs[i].Job_status == JOB_RUNNING)
            {
                sleep(5);
                jobs[i].Job_status = JOB_COMPLETED;
                completed_jobs++;
                // printf("Job %d completed successfully.\n", jobs[i].job_Id);
                // fflush(stdout);
            }
            i++;
        }
    }
    printf("all jobs completed successfully.\n");
    return NULL;
}

// Below all functions hanldes the arithmetic operations.
// to test the rpc functionality whether it is making rpc calls or not.
void Addition_Operation(int x, int y, CLIENT *Client)
{
    operands Operands = {x, y};
    int *Arith_result = add_1(&Operands, Client);
    if (Arith_result)
        printf("Addition Result: %d\n", *Arith_result);
    else
    {
        printf("error ocurred while performing addition operartion\n");
    }
    fflush(stdout);
}

void Exponentiation(int x, int y, CLIENT *Client)
{
    operands Operands = {x, y};
    int *Arith_result = power_1(&Operands, Client);
    if (Arith_result)
        printf("Exponentiation Result: %d\n", *Arith_result);

    else
    {
        printf("error ocurred while performing exponentiation operartion\n");
    }
    fflush(stdout);
}

void Multiplication_Operation(int x, int y, CLIENT *Client)
{
    operands Operands = {x, y};
    int *Arith_result = multiply_1(&Operands, Client);
    if (Arith_result)
        printf("Multiplication Result: %d\n", *Arith_result);
    else
    {
        printf("error ocurred while performing multiplication operartion\n");
    }
    fflush(stdout);
}

void Subtraction_Operation(int x, int y, CLIENT *Client)
{
    operands Operands = {x, y};
    int *Arith_result = subtract_1(&Operands, Client);
    if (Arith_result)
        printf("Subtraction Result: %d\n", *Arith_result);
    else
    {
        printf("error ocurred while performing subtraction operartion\n");
    }
    fflush(stdout);
}

int main()
{
    // variable declaration
    int x, y;
    char Inp_Command[256];
    char hostname[128];

    Read_Servers_by_Hostnames();
    Show_Help_Command();

    pthread_t Cpu_Thread_Id;
    pthread_create(&Cpu_Thread_Id, NULL, Monitor_Thread_CPU, NULL);

    while (1)
    {
        printf("Console> ");
        fflush(stdout);
        fgets(Inp_Command, sizeof(Inp_Command), stdin);
        Inp_Command[strcspn(Inp_Command, "\n")] = '\0';

        // exits shell when command is exit
        if (strcmp(Inp_Command, "exit") == 0)
        {
            break;
        }
        else if (strcmp(Inp_Command, "help") == 0)
        {
            Show_Help_Command();
        }

        // stops the specific server
        else if (strncmp(Inp_Command, "stop", strlen("stop")) == 0)
        {

            sscanf(Inp_Command + strlen("stop"), "%127s", hostname);
            Stop_Server_By_Hostname(hostname);
        }

        // below condition for start the specific server
        else if (strncmp(Inp_Command, "start", strlen("start")) == 0)
        {
            sscanf(Inp_Command + strlen("start"), "%127s", hostname);
            start_specific_server(hostname);
        }

        else if (strcmp(Inp_Command, "cpuload") == 0)
        {
            int i = 0;

            while (i < NUM_OF_SERVERS)
            {
                if (servers[i].Server_Active_Status)
                {
                    // making a rpc call to server to fetch the cpuload of the server.
                    // below functions handles to fetch the server load.
                    // it fetches the cpuload only for the active servers.
                    cpu_load *load_res = get_cpu_load_1(NULL, servers[i].clnt);
                    if (!load_res)
                    {
                        fprintf(stderr, "failed while fetching cpuload for server %s.\n", servers[i].hostname);
                        // fflush(stderr);
                        continue;
                    }
                    if (load_res && load_res->load_avg >= 0.0)
                        printf("%s: %.2f\n", servers[i].hostname, load_res->load_avg);
                    else
                        printf("%s: Cannot fetch cpuload.\n", servers[i].hostname);
                    fflush(stdout);
                }
                i++;
            }
        }
        else if (strncmp(Inp_Command, "hyper_link", strlen("hyper_link")) == 0)
        {
            int start, end, step;
            pthread_t job_tid;

            sscanf(Inp_Command + strlen("hyper_link"), "%d %d %d", &start, &end, &step);

            JobArguments *Job_ARGS = malloc(sizeof(JobArguments));

            if (!Job_ARGS)
            {
                perror("Error occured while allocating memory to job arguments");
                continue;
            }

            Job_ARGS->step = step;
            Job_ARGS->start = start;
            Job_ARGS->end = end;

            if (pthread_create(&job_tid, NULL, run_job_thread, (void *)Job_ARGS) != 0)
            {
                perror("failed to create the thread for job");
                free(Job_ARGS);
                continue;
            }

            pthread_detach(job_tid);
            printf("Jobs started in background. the console will accpet new commands to test the cpuload or any.\n");
            fflush(stdout);
        }
        else if (sscanf(Inp_Command, "multiply %d %d", &x, &y) == 2)
        {
            Multiplication_Operation(x, y, servers[0].clnt);
        }

        else if (sscanf(Inp_Command, "add %d %d", &x, &y) == 2)
        {
            Addition_Operation(x, y, servers[0].clnt);
        }
        else if (sscanf(Inp_Command, "subtract %d %d", &x, &y) == 2)
        {
            Subtraction_Operation(x, y, servers[0].clnt);
        }
        else if (sscanf(Inp_Command, "power %d %d", &x, &y) == 2)
        {
            Exponentiation(x, y, servers[0].clnt);
        }
        else if (strncmp(Inp_Command, "set_lowthresh", strlen("set_lowthresh")) == 0)
        {
            float new_threshold;
            if (sscanf(Inp_Command + strlen("set_lowthresh"), "%f", &new_threshold) == 1)
            {
                if (new_threshold >= 0.0 && new_threshold <= 100.0)
                {
                    SET_CPU_LOW_THRESHOLD = new_threshold;
                    printf("Low CPU threshold set to %.2f\n", SET_CPU_LOW_THRESHOLD);
                }
                else
                {
                    printf("Invalid threshold value. Please enter x value between 0 and 100.\n");
                }
                fflush(stdout);
            }
            else
            {
                printf("Invalid Inp_Command format. Use: set_lowthresh <value>\n");
                fflush(stdout);
            }
        }
        else if (strcmp(Inp_Command, "status") == 0)
        {
            for (int i = 0; i < NUM_OF_SERVERS; i++)
            {
                printf(" %s : %s\n", servers[i].hostname, servers[i].Server_Active_Status ? "Active" : "Inactive");
            }
        }

        else if (strncmp(Inp_Command, "set_highthresh", strlen("set_highthresh")) == 0)
        {
            float new_threshold;
            if (sscanf(Inp_Command + strlen("set_highthresh"), "%f", &new_threshold) == 1)
            {
                if (new_threshold >= 0.0 && new_threshold <= 100.0)
                {
                    SET_CPU_HIGH_THRESHOLD = new_threshold;
                    printf("High CPU threshold set to %.2f\n", SET_CPU_HIGH_THRESHOLD);
                }
            }
            else
            {
                printf("Invalid input command format. Usage: set_highthresh <value>\n");
                fflush(stdout);
            }
        }

        else
        {
            printf("Unknown input command. Type 'help' to list the available commands.\n");
            fflush(stdout);
        }
    }

    // After completing all the jobs then a mutex lock is set and destroying all the servers.
    pthread_mutex_lock(&Mutex_Server);
    for (int i = 0; i < NUM_OF_SERVERS; i++)
    {
        if (servers[i].clnt)
            clnt_destroy(servers[i].clnt);
    }
    pthread_mutex_unlock(&Mutex_Server);

    return 0;
}