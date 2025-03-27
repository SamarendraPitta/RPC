struct job_args {
    int job_id;      
    int start;       
    int end;         
    int step;        
};

struct cpu_load {
    float load_avg;  
};

struct server_status {
    int is_active; 
};

struct operands {
    int a;
    int b;
};

program REPLICATOR_PROG {
    version REPLICATOR_VERS {
        int RUN_JOB(job_args) = 1;        
        cpu_load GET_CPU_LOAD(void) = 2; 
        int STOP_SERVER(void) = 3;        
        int START_SERVER(void) = 4;

        int ADD(operands) = 5;
        int SUBTRACT(operands) = 6;
        int MULTIPLY(operands) = 7;
        int POWER(operands) = 8;

    } = 1;
} = 0x31234567;
