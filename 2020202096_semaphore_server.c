#define _GNU_SOURCE
///////////////////////////////////////////////////////////////////////
// File Name : 2020202096_semaphore_server.c                         //
// Date : 2023/05/31                                                 //
// Os : Ubuntu 16.04 LTS 64bits                                      //
// Author : Woo Sung Won                                             //
// Student ID : 2020202096                                           //
// ----------------------------------------------------------------- //
// Title : System Programming Assignment #3-3                        //
// Description : Make semaphore server                               //
///////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <glob.h>
#include <fnmatch.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/time.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <fcntl.h>


#define URL_LEN 256
#define BUFFSIZE 10240
#define PORTNO 40000
#define MAXCLIENT 10
#define SHM_KEY 40000

void sizeBubbleSort(unsigned int *sizeary, char *str[], int height, int weight, int isRflag);
char *extractNonHidden(char *origin);
char *extractRelativeDir(char *origin);
void initHeightWeight(int *w, int *h, char *dir, int isShowHidden);
void list_directory(int isBackward, FILE *stream, char *dir_path, int isShowHidden, int isHflag, int isSflag, int isRflag);
void optNotEqualPrint(int isBackward, FILE *stream, int a_flag, int l_flag, int foldercnt, int filecnt, char **folderarray, char **filearray, int optcnt, int isHflag, int isSflag, int isRflag);
void printAllOption(FILE *stream, char *directory, int isShowHidden);
void printLFiles(FILE *stream, char *dir, int isHflag, int isSflag, int isRflag);
void bubbleSort(char *str[], int height, int weight);
const char *get_mimetype(const char *filename);

int maxChilds = 0;
int maxIdleNum = 0;
int minIdleNum = 0;
int startProcess = 0;
int maxHistory = 0;



// Declare a client struct
typedef struct client
{
    int no;                   // client number
    char ip[INET_ADDRSTRLEN]; // IP address of the client
    int port;                 // port number of the client
    int pid;                  // process ID of the client
    time_t time;              // time when the client connected
} client;

//client client_list[10][MAXCLIENT]; // array to store the clients
time_t cur_time;
char *c_time;

typedef struct {
    int historycount;
    client history[10];
    int idleprocesscount;
    pid_t idlepids[10];
    int nonidlecount;
} SharedMemory;

SharedMemory *shared_memory;
pthread_mutex_t mutex;

FILE* log_file;
sem_t* semaphore;
///////////////////////////////////////////////////////////////////////
// initialize_shared_memory                                          //
// ================================================================= //
// Output: void                                                      //
// Purpose: Initializes shared memory for interprocess communication //
// and sets initial values. This function uses System V shared memory//
// API for creating shared memory segment.                           //
///////////////////////////////////////////////////////////////////////
void initialize_shared_memory() {
    int shmid = shmget(SHM_KEY, sizeof(SharedMemory) + sizeof(client) * 10, IPC_CREAT | 0666);

    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }

    shared_memory = shmat(shmid, NULL, 0);
    if (shared_memory == (SharedMemory *) -1) {
        perror("shmat");
        exit(1);
    }
    
    shared_memory->historycount = 0;
    shared_memory->idleprocesscount=0;
    shared_memory->nonidlecount=0;
}
///////////////////////////////////////////////////////////////////////
// cleanup_shared_memory                                             //
// ================================================================= //
// Output: void                                                      //
// Purpose: Releases and removes shared memory from the system. This //
// function uses System V shared memory API for removing shared      //
// memory segment.                                                   //
///////////////////////////////////////////////////////////////////////
void cleanup_shared_memory() {
    shmdt(shared_memory);
    shmctl(SHM_KEY, IPC_RMID, NULL);
}
///////////////////////////////////////////////////////////////////////
// add_history                                                       //
// ================================================================= //
// Input: client new_client                                          //
// -> New client data that should be added to history                //
// Output: void                                                      //
// Purpose: Adds a new client's data to the history in shared memory //
// The function also manages history size to not exceed maximum      //
// allowed size.                                                     //
///////////////////////////////////////////////////////////////////////
void add_history(client new_client) {
    pthread_mutex_lock(&mutex);

    // Increase the no of existing clients
    for (int i = 0; i < shared_memory->historycount; i++) {
        shared_memory->history[i].no++;
    }

    // Check if the history is full
    if (shared_memory->historycount >= maxHistory) {
        // Remove the oldest client
        for (int i = 0; i < maxHistory - 1; i++) {
            shared_memory->history[i] = shared_memory->history[i + 1];
        }
        shared_memory->historycount = maxHistory - 1;  // Decrease the count
    }

    // Add new client at the start of the history with no 1
    new_client.no = 1;
    shared_memory->history[shared_memory->historycount++] = new_client;

    pthread_mutex_unlock(&mutex);
}
///////////////////////////////////////////////////////////////////////
// print_history                                                     //
// ================================================================= //
// Output: void                                                      //
// Purpose: Prints connection history from shared memory. The        //
// function prints history in reverse order, i.e., most recent       //
// connections are printed first.                                    //
///////////////////////////////////////////////////////////////////////
void print_history() {
    pthread_mutex_lock(&mutex);

    printf("========== Connection History ==========\n");
    printf("No.\tIP\t\tPort\tPID\tTime\n");
    for (int i = shared_memory->historycount - 1; i >= 0; i--) {
        client c = shared_memory->history[i];
        printf("%d\t%s\t%d\t%d\t%s", c.no, c.ip, c.port, c.pid, ctime(&c.time));
    }
    printf("========================================\n");

    pthread_mutex_unlock(&mutex);
}
///////////////////////////////////////////////////////////////////////
// add_write                                                         //
// ================================================================= //
// Input: void* arg                                                  //
// -> Pointer to the log message to write into the log file          //
// Output: void*                                                     //
// -> Not used in this function                                      //
// Purpose: Adds a log message to the log file. The function uses    //
// semaphore for synchronization to prevent concurrent writes to the //
// log file.                                                         //
///////////////////////////////////////////////////////////////////////
void* add_write(void* arg){
    char* log_message = (char*)arg;
    semaphore=sem_open("semaphore",O_RDWR);
    sem_wait(semaphore); // 세마,포어 대기
    // 로그 파일에 클라이언트 연결 정보 작성
    fprintf(log_file, "%s", log_message);
    fflush(log_file); // 버퍼 비우기
    sem_post(semaphore); // 세마포어 신호

}





///////////////////////////////////////////////////////////////////////
// alarm_handler                                                     //
// ================================================================= //
// Input: int signo                                                  //
// -> The signal number of the signal that triggered the handler     //
// Output: void                                                      //
// -> This function does not return a value                          //
// Purpose: To print connection history every time a signal is       //
// received and set a new alarm for the next signal                  //
///////////////////////////////////////////////////////////////////////
void alarm_handler(int signo)
{
   // print the connection history
    print_history();

    // set the alarm to trigger after 10 seconds
    alarm(10);
}
//////////////////////////////////////////////////////////////////////////
// is_accessible                                                        //
// =====================================================================//
// Input: const char *ip                                                //
// -> The IP address that needs to be checked for accessibility         //
// Output: int                                                          //
// -> Returns 1 if the IP is accessible, 0 otherwise                    //
// Purpose: Check if the given IP is listed as accessible in a specific //
// file ("accessible.usr")                                              //
//////////////////////////////////////////////////////////////////////////
int is_accessible(const char *ip)
{
    // open the file "accessible.usr" for reading
    FILE *file = fopen("accessible.usr", "r");
    if (file == NULL)
    {
        // print an error message if the file could not be opened
        perror("fopen");
        return 0;
    }

    char line[INET_ADDRSTRLEN];
    // read the file line by line
    while (fgets(line, sizeof(line), file) != NULL)
    {
        // remove the newline character from the line
        line[strcspn(line, "\n")] = '\0';

        // use the fnmatch function to match the line with the IP
        // if the line matches with the IP, return 1
        if (fnmatch(line, ip, 0) == 0)
        {
            fclose(file);
            return 1;
        }
    }

    // close the file and return 0 if no match was found
    fclose(file);
    return 0;
}

///////////////////////////////////////////////////////////////////////
// read_childproc                                                    //
// ================================================================= //
// Input: int sig                                                    //
// -> The signal number of the signal that triggered the handler     //
// Output: void                                                      //
// -> This function does not return a value                          //
// Purpose: To reap child processes that have terminated to prevent  //
// zombie processes. This function is used as a signal handler for   //
// the SIGCHLD signal.                                               //
///////////////////////////////////////////////////////////////////////
void read_childproc(int sig)
{
    // Declare variables to hold the process ID of the child process
    // and the status information of the child process
    pid_t pid;
    int status;

    // The waitpid call waits for any child process to end, returning immediately if none have with the WNOHANG option.
    pid = waitpid(-1, &status, WNOHANG);
}

///////////////////////////////////////////////////////////////////////
// sigint_handler                                                    //
// ================================================================= //
// Input: int sig                                                    //
// -> The signal number of the signal that triggered the function    //
// Output: void                                                      //
// -> This function does not return a value                          //
// Purpose: To terminate all child processes and the server itself   //
// when a SIGINT signal (Ctrl+C) is received                         //
///////////////////////////////////////////////////////////////////////
void sigint_handler(int sig)
{
    printf("check\n");
    pthread_mutex_lock(&mutex);
    cur_time = time(NULL);
    c_time = ctime(&(cur_time));
    c_time[strlen(c_time) - 1] = '\0';
    for (int i = 0; i < shared_memory->idleprocesscount; i++)  // Iterate through the child processes
    {
        printf("[%s] %ld process is terminated.\n", c_time, (long)shared_memory->idlepids[i]);  // Print a message indicating the termination of a process
        kill(shared_memory->idlepids[i], SIGTERM);  // Send SIGTERM signal to the child process to terminate it
        waitpid(shared_memory->idlepids[i], NULL, 0);  // Wait for the child process to terminate
    }
    pthread_mutex_unlock(&mutex);
    printf("[%s] Server is terminated.\n", c_time);  // Print a message indicating the termination of the server
    exit(0);  // Exit the program
}

///////////////////////////////////////////////////////////////////////
// term_exit                                                         //
// ================================================================= //
// Input: int sig                                                    //
// -> The signal number of the signal that triggered the function    //
// Output: void                                                      //
// -> This function does not return a value                          //
// Purpose: To terminate the program when a specific signal is       //
// received                                                          //
///////////////////////////////////////////////////////////////////////
void term_exit(int sig)
{
    exit(0);  // Exit the program
}


///////////////////////////////////////////////////////////////////////
// addIdleProcess                                                    //
// ================================================================= //
// Input: pid_t pid                                                  //
// -> Process ID of the child process to add to idle processes list  //
// Output: void                                                      //
// Purpose: Adds a process to the idle processes list in shared      //
// memory. The function also manages size of idle processes list to  //
// not exceed maximum allowed size.                                  //
///////////////////////////////////////////////////////////////////////
void addIdleProcess(pid_t pid) {
    pthread_mutex_lock(&mutex);

    cur_time = time(NULL);
    c_time = ctime(&(cur_time));
    c_time[strlen(c_time) - 1] = '\0';

    char buffer_thread[1024];

    shared_memory->idlepids[shared_memory->idleprocesscount] = pid;
    shared_memory->idleprocesscount++;
    printf("[%s] IdleProcessCount : %d\n",c_time,shared_memory->idleprocesscount);

    sprintf(buffer_thread,"[%s] IdleProcessCount : %d\n",c_time,shared_memory->idleprocesscount);        
    add_write(buffer_thread);

    if (shared_memory->idleprocesscount > maxIdleNum)
    {
        while (shared_memory->idleprocesscount > 5)
        {
            // Kill the first process in the array
            kill(shared_memory->idlepids[0], SIGKILL);
            printf("[%s] %d idleprocess is terminated.\n", c_time, shared_memory->idlepids[0]);

            sprintf(buffer_thread, "[%s] %d idleprocess is terminated.\n", c_time, shared_memory->idlepids[0]);
            add_write(buffer_thread);

            // Shift the remaining elements of the array
            for (int i = 0; i < shared_memory->idleprocesscount - 1; i++)
                shared_memory->idlepids[i] = shared_memory->idlepids[i + 1];

            shared_memory->idleprocesscount--;
            printf("[%s] IdleProcessCount : %d\n", c_time, shared_memory->idleprocesscount);

            sprintf(buffer_thread, "[%s] IdleProcessCount : %d\n", c_time, shared_memory->idleprocesscount);
               add_write(buffer_thread);
        }
    }

    pthread_mutex_unlock(&mutex);
}
///////////////////////////////////////////////////////////////////////
// removeIdleProcess                                                 //
// ================================================================= //
// Input: pid_t pid                                                  //
// -> Process ID of the child process to remove from idle processes  //
// list                                                              //
// Output: void                                                      //
// Purpose: Removes a process from the idle processes list in shared //
// memory.                                                           //
///////////////////////////////////////////////////////////////////////
void removeIdleProcess(pid_t pid) {
    pthread_mutex_lock(&mutex);
    cur_time = time(NULL);
    c_time = ctime(&(cur_time));
    c_time[strlen(c_time) - 1] = '\0';

    char buffer_thread[1024];

    for (int i = 0; i < shared_memory->idleprocesscount; i++) {
        if (shared_memory->idlepids[i] == pid) {
            // Shift idle pids to remove the specified pid
            for (int j = i; j < shared_memory->idleprocesscount - 1; j++) {
                shared_memory->idlepids[j] = shared_memory->idlepids[j + 1];
            }
            shared_memory->idleprocesscount--;
            printf("[%s] IdleProcessCount : %d\n",c_time,shared_memory->idleprocesscount);

            sprintf(buffer_thread, "[%s] IdleProcessCount : %d\n", c_time, shared_memory->idleprocesscount);
              add_write(buffer_thread);
            break;
        }
    }

  
    pthread_mutex_unlock(&mutex);
}
///////////////////////////////////////////////////////////////////////
// isForkNeeded                                                      //
// ================================================================= //
// Output: int                                                       //
// -> Returns 1 if forking of a new process is needed, 0 otherwise   //
// Purpose: Checks if there is a need for forking a new process      //
// based on the current number of idle processes.                    //
///////////////////////////////////////////////////////////////////////
int isForkNeeded(){
    pthread_mutex_lock(&mutex);
    if ((minIdleNum > shared_memory->idleprocesscount) )
    {
        printf("true\n");
        return 1;
    }
    else
    {
        return 0;
    }
    pthread_mutex_unlock(&mutex);
}
///////////////////////////////////////////////////////////////////////
// get_idle_process_count                                            //
// ================================================================= //
// Output: int                                                       //
// -> Returns the current number of idle processes                   //
// Purpose: To provide a way to get the current number of idle       //
// processes.                                                        //
///////////////////////////////////////////////////////////////////////
int get_idle_process_count() {
    pthread_mutex_lock(&mutex);

    int count = shared_memory->idleprocesscount;

    pthread_mutex_unlock(&mutex);

    return count;
}

///////////////////////////////////////////////////////////////////////
// get_idle_process_count                                            //
// ================================================================= //
// Output: int                                                       //
// -> Returns the current number of idle processes                   //
// Purpose: To provide a way to get the current number of idle       //
// processes.                                                        //
///////////////////////////////////////////////////////////////////////
void addNonIdleCount() {
    pthread_mutex_lock(&mutex);

    shared_memory->nonidlecount++;

    pthread_mutex_unlock(&mutex);
}
///////////////////////////////////////////////////////////////////////
// removeNonIdleCount                                                //
// ================================================================= //
// Output: void                                                      //
// Purpose: Decreases the count of non-idle processes. This function //
// is thread-safe.                                                   //
///////////////////////////////////////////////////////////////////////
void removeNonIdleCount() {
    pthread_mutex_lock(&mutex);

    shared_memory->nonidlecount--;

    pthread_mutex_unlock(&mutex);
}




// Main Function
int main(int argc, char *argv[])
{

    pthread_t thread_id;

    FILE *file = fopen("httpd.conf", "r");
    if (file == NULL) {
        perror("fopen");
        exit(1);
    }
    // 로그 파일 열기
    log_file = fopen("server_log.txt", "a");
     // 세마포어 초기화
    semaphore = sem_open("semaphore", O_CREAT, 0644, 1);
    sem_close(semaphore);
    if (semaphore == SEM_FAILED) {
        perror("sem_open");
        return 1;
    }
    if (log_file == NULL) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    char line[256];
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, "MaxChilds:", 10) == 0) {
            maxChilds = atoi(line + 10);
        } else if (strncmp(line, "MaxIdleNum:", 11) == 0) {
            maxIdleNum = atoi(line + 11);
        } else if (strncmp(line, "MinIdleNum:", 11) == 0) {
            minIdleNum = atoi(line + 11);
        } else if (strncmp(line, "StartProcess:", 13) == 0) {
            startProcess = atoi(line + 13);
        } else if (strncmp(line, "MaxHistory:", 11) == 0) {
            maxHistory = atoi(line + 11);
        }
    }

    fclose(file);

   
    char buffer_thread[1024];

    // Declare variables
    struct sockaddr_in server_addr, client_addr;
    int server_fd, client_fd;
    int len, len_out;
    int opt = 1;
    struct stat checkstat;

    cur_time = time(NULL);
    c_time = ctime(&(cur_time));
    c_time[strlen(c_time) - 1] = '\0';

    pid_t pid;
    struct sigaction act;
    int state;

    // Create a socket
    if ((server_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("server: Can't open stream socket.");
        return 0;
    }
    // Assign the signal handler function (read_childproc) to the sa_handler field of the sigaction struct
    act.sa_handler = read_childproc;

    // Clear all bits in the signal set represented by sa_mask, so no signals are blocked during execution of the signal handler
    sigemptyset(&act.sa_mask);

    // Set the sa_flags field of the sigaction struct to 0, meaning no special behavior is specified
    act.sa_flags = 0;

    // Install the signal handler for the SIGCHLD signal using the sigaction function
    // If successful, sigaction returns 0, and the state variable will hold this value
    state = sigaction(SIGCHLD, &act, 0);

    // Set socket options
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Clear server address
    memset(&server_addr, 0, sizeof(server_addr));

    // Set server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORTNO);

    // Bind socket to server address
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("server: can't bind local addres\n");
        return 0;
    }else{
        printf("[%s] Server is started\n", c_time);
        sprintf(buffer_thread,"[%s] Server is started\n", c_time);
            
        pthread_t server_start;
        pthread_create(&server_start, NULL, add_write, buffer_thread);
        pthread_join(server_start, NULL);
    }

    // Listen for connections
    listen(server_fd, 5);

    
    len = sizeof(client_addr);

    
    initialize_shared_memory();
    pthread_mutex_init(&mutex, NULL);


    // Add Signal for Alarm that print history every 10 seconds
    signal(SIGALRM, alarm_handler);
    alarm(10);
    signal(SIGINT, sigint_handler);

    
    // Pre-forking routine
    for (int i = 0; i < startProcess; i++)
    {
        int client_count = 0;
        if ((pid = fork()) > 0)
        {
            printf("[%s] %ld process is forked.\n", c_time, (long)pid);
            sprintf(buffer_thread,"[%s] %d process is forked.\n", c_time, pid);
            
            pthread_t add_parent_fork;
            pthread_create(&add_parent_fork, NULL, add_write, buffer_thread);
            pthread_join(add_parent_fork, NULL);
            addIdleProcess(pid);
        }
        else if (pid == 0)
        {
            pid_t childpid = getpid();
            // Handle incoming connections
            signal(SIGUSR1, print_history);
            signal(SIGTERM, term_exit);
            signal(SIGINT, SIG_IGN);
            while (1)
            {
                // Declare variables
                struct in_addr inet_client_address;
                char *buffer = NULL;
                size_t buffer_size = 0;
                FILE *stream = open_memstream(&buffer, &buffer_size);
                char buf[BUFFSIZE] = {
                    0,
                };
                char tmp[BUFFSIZE] = {
                    0,
                };
                char *url = NULL;
                char method[BUFFSIZE] = {
                    0,
                };
                char *tok = NULL;

                char tempbuf[BUFFSIZE] = {
                    0,
                };
                // Accept a new connection
                len = sizeof(client_addr);
                client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &len);

                read(client_fd, buf, BUFFSIZE);
                strcpy(tempbuf, buf);
                char *testok = strtok(tempbuf, " ");

                // If accept fails, skip this iteration
                if (client_fd < 0)
                {
                    continue;
                }

                // If IP is accessible, create and set up a new client
                if (is_accessible(inet_ntoa(client_addr.sin_addr)) && testok != NULL)
                {
                    // time
                    cur_time = time(NULL);
                    c_time = ctime(&(cur_time));
                    c_time[strlen(c_time) - 1] = '\0';

                    client new_client;
                    new_client.no = shared_memory->historycount + 1;
                    strcpy(new_client.ip, inet_ntoa(client_addr.sin_addr));
                    new_client.port = ntohs(client_addr.sin_port);
                    new_client.pid = getpid();
                    new_client.time = cur_time;

                    // Add new client to shared memory history
                    add_history(new_client);

                    // Get client IP address and print a message
                    inet_client_address.s_addr = client_addr.sin_addr.s_addr;

                    // Read client request

                    // Declare variables
                    char curdir[10000];
                    int isBackward = 0;

                    // Get current working directory
                    if (getcwd(curdir, sizeof(curdir)) == NULL)
                    {
                        perror("getcwd() error");
                    }

                    // Copy request to temporary buffer
                    strcpy(tmp, buf);

                    // Parse request method and URL
                    tok = strtok(tmp, " ");
                    strcpy(method, tok);
                    if (strcmp(method, "GET") == 0)
                    {
                        tok = strtok(NULL, " ");

                        url = extractNonHidden(tok);
                        if (*(url + strlen(url) - 1) == '/')
                        {
                            *(url + strlen(url) - 1) = '\0';
                            isBackward = 1;
                        }
                    }
                    
                    // Create a title for the HTML response
                    int len = strlen(inet_ntoa(inet_client_address)) + strlen("40000") + strlen(url) + 2;
                    char *title = (char *)malloc(len * sizeof(char));

                    snprintf(title, len, "%s:%s%s", inet_ntoa(inet_client_address), "40000", url);

                    if (url[0] == '\0') // if the requested URL is empty
                    {
                        url = "."; // set it to the current directory
                    }

                    printf("\n========== New Client ==========\n");
                    printf("TIME : [%s]\n", c_time);
                    printf("URL : %s\n",url);
                    printf("IP: %s\n", new_client.ip);
                    printf("Port: %d\n", new_client.port);
                    printf("================================\n");

                    char buffer_thread[1024];
                    int offset = 0;

                    offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "\n========== New Client ==========\n");
                    offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "TIME : [%s]\n", c_time);
                    offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "URL : %s\n",url);
                    offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "IP: %s\n", new_client.ip);
                    offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "Port: %d\n", new_client.port);
                    offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "================================\n");
                    struct timeval start, end;
                    gettimeofday(&start, NULL);  // 연결 시작 시간 기록

                    pthread_t add_parent_fork;
                    pthread_create(&add_parent_fork, NULL, add_write, buffer_thread);
                    pthread_join(add_parent_fork, NULL);

                    removeIdleProcess(childpid);
                    addNonIdleCount();

                    if (lstat(url, &checkstat) == -1) // check the status of the requested URL
                    {
                        // perror("404 Not Found\n"); // print an error message
                        fprintf(stream,
                                "HTTP/1.1 200 OK\r\n"
                                "Content-Type: text/html; charset=UTF-8\r\n\r\n");
                        fprintf(stream, "<html>\n<head>\n");
                        fprintf(stream, "<link rel=\"icon\" href=\"data:,\">\n"); // add a favicon
                        fprintf(stream, "<title>%s</title>\n", curdir);           // set the page title to the current directory
                        fprintf(stream, "</head>\n<body>\n");
                        fprintf(stream, "<h1>");
                        fprintf(stream, "Not Found");
                        fprintf(stream, "</h1>");
                        fprintf(stream, "<b>The request URL %s was not found on this server<br></b>", url);
                        fprintf(stream, "<b>HTTP 404 - Not Page Found</b>");
                    }
                    else // if the requested URL exists
                    {
                        if ((strcmp(url, ".")) == 0) // if the requested URL is the current directory
                        {

                            argc = 3;
                            argv[0] = "ls";
                            argv[1] = "-l";
                            argv[2] = url; // set arguments to list the files in the directory
                        }
                        else // if the requested URL is a file or directory
                        {
                            if (S_ISDIR(checkstat.st_mode)) // if the requested URL is a directory
                            {
                                argc = 3;
                                argv[0] = "ls";
                                argv[1] = "-al";
                                argv[2] = url; // set arguments to list the files in the directory
                            }
                            else // if the requested URL is a file
                            {
                                char *mimestream_buffer = NULL;
                                size_t mimestream_buffer_size = 0;
                                const char *mimetype = get_mimetype(url); // get the MIME type of the requested file
                                FILE *mimestream = open_memstream(&mimestream_buffer, &mimestream_buffer_size);
                                // find whether if filename is a symbolic link file
                                int symlink = 0;
                                struct stat file_stat;
                                if (lstat(url, &file_stat) == 0)
                                {
                                    symlink = S_ISLNK(file_stat.st_mode);
                                }
                                if (symlink) // If the file is a symbolic link
                                {
                                    char target[PATH_MAX];
                                    ssize_t len = readlink(url, target, sizeof(target) - 1);
                                    if (len != -1)
                                    {
                                        target[len] = '\0';
                                        mimetype = get_mimetype(target);
                                        url = target;
                                    }
                                }
                                if (mimetype)
                                {
                                    fprintf(mimestream, "HTTP/1.1 200 OK\n");
                                    fprintf(mimestream, "Content-Type: %s\n\n", mimetype); // set the MIME type header
                                    FILE *file = fopen(url, "rb");
                                    if (file)
                                    {
                                        char buffer[1024];
                                        size_t bytes;
                                        while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0)
                                        {
                                            fwrite(buffer, 1, bytes, mimestream); // write the file contents to the stream
                                        }
                                        fclose(file);
                                    }
                                    else // if the file cannot be opened
                                    {
                                        fprintf(mimestream, "HTTP/1.1 404 Not Found\n");
                                        fprintf(mimestream, "Content-Type: text/html\n\n");
                                        fprintf(mimestream, "<html><body><h1>404 Not Found</h1></body></html>\n"); // print an error message
                                    }
                                }
                                else // if the MIME type is not supported
                                {
                                    fprintf(mimestream, "HTTP/1.1 Unsupported Media Type\n");
                                    fprintf(mimestream, "Content-Type: text/html\n\n");
                                    fprintf(mimestream, "<html><body><h1>Unsupported Media Type</h1></body></html>\n");
                                }
                                fclose(mimestream); // Close mimestream

                                cur_time = time(NULL);
                                c_time = ctime(&(cur_time));
                                c_time[strlen(c_time) - 1] = '\0';

                                  // 연결 종료 시간 기록
                                gettimeofday(&end, NULL);

                                long long elapsed_time = (end.tv_sec - start.tv_sec) * 1000000LL + (end.tv_usec - start.tv_usec);
                                printf("\n====== Disconnected Client =======\n");
                                printf("TIME : [%s]\n", c_time);
                                printf("URL : %s\n",url);
                                printf("IP: %s\n", new_client.ip);
                                printf("Port: %d\n", new_client.port);
                                printf("CONNECTING TIME: %lld \n", elapsed_time);
                                printf("==================================\n");
                               
                               

                                char buffer_thread[1024];
                                int offset = 0;

                                offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "\n====== Disconnected Client =======\n");
                                offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "TIME : [%s]\n", c_time);
                                offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "URL : %s\n",url);
                                offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "IP: %s\n", new_client.ip);
                                offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "Port: %d\n", new_client.port);
                                offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "CONNECTING TIME: %lld \n", elapsed_time);
                                offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "==================================\n");

                                pthread_t add_parent_fork;
                                pthread_create(&add_parent_fork, NULL, add_write, buffer_thread);
                                pthread_join(add_parent_fork, NULL);

                                // Write mimestream_buffer to client_fd; print error if necessary
                                if (write(client_fd, mimestream_buffer, (mimestream_buffer_size)) == -1)
                                {
                                    perror("write error");
                                };

                                

                                close(client_fd); // Close client file descriptor
                                sleep(5);

                                addIdleProcess(childpid);
                                removeNonIdleCount();
                                continue;         // Go to next iteration
                            }
                        }
                        // Send HTTP response header
                        fprintf(stream,
                                "HTTP/1.1 200 OK\r\n"
                                "Content-Type: text/html; charset=UTF-8\r\n\r\n");

                        // Start HTML structure
                        fprintf(stream, "<html>\n<head>\n");
                        fprintf(stream, "<link rel=\"icon\" href=\"data:,\">\n");
                        fprintf(stream, "<title>%s</title>\n", curdir);
                        fprintf(stream, "</head>\n<body>\n");
                        fprintf(stream, "<h1>");
                        if ((strcmp(url, ".")) == 0) // if the requested URL is the current directory
                        {
                            fprintf(stream, "Welcome to System Programming Http");
                        }
                        else
                        {
                            fprintf(stream, "System Programming Http");
                        }
                        fprintf(stream, "</h1>");

                        // Declaring variables

                        int a_flag = 0;
                        int l_flag = 0;
                        int h_flag = 0, S_flag = 0, r_flag = 0;
                        struct stat file_stat;
                        glob_t pglob;
                        // Declaring pointers for storing arguments and options
                        char **optarr;
                        char **folderarray;
                        char **filearray;

                        // Initializing variables
                        int optcnt = 0;
                        int maxoptlen = 0;
                        int filecnt = 0;
                        int foldercnt = 0;

                        // Allocating memory for optarr
                        optarr = (char **)malloc(sizeof(char *) * argc);
                        optind = 1;

                        // Parsing command line options using getopt()
                        while ((opt = getopt(argc, argv, "alhSr")) != -1)
                        {
                            switch (opt)
                            {
                            case 'a': // If -a option is provided
                                a_flag = 1;
                                break;
                            case 'l': // If -l option is provided
                                l_flag = 1;
                                break;
                            case 'h': // If -h option is provided
                                h_flag = 1;
                                break;
                            case 'S': // If -S option is provided
                                S_flag = 1;
                                break;
                            case 'r': // If -r option is provided
                                r_flag = 1;
                                break;

                            default: // If invalid option is detected
                                printf("Invalid option detected\n");
                            }
                        }

                        // Storing non-option arguments in optarr
                        for (int i = optind; i < argc; i++)
                        {
                            char *arg = argv[i];
                            if (arg != NULL && (lstat(arg, &file_stat) != -1))
                            {
                                // Finding max option length
                                if (maxoptlen < strlen(arg) + 1)
                                {
                                    maxoptlen = strlen(arg) + 1;
                                }

                                // Allocating memory for optarr
                                optarr[optcnt] = (char *)malloc(sizeof(char) * (maxoptlen));

                                // Copying the argument to optarr
                                strcpy(optarr[optcnt], arg);
                                optcnt++;
                            }
                            // If argument is not an option and is not a valid file path
                            else if (arg[0] != '-' && (access(arg, F_OK) != 0))
                            {
                                // If argument contains wildcard characters
                                if (strchr(arg, '*') != NULL || strchr(arg, '?') != NULL || strchr(arg, '[') != NULL || strchr(arg, ']') != NULL)
                                {
                                    // Set flags for globbing
                                    int flags;
                                    flags = GLOB_MARK | GLOB_TILDE;

                                    // Perform globbing to get list of matching files
                                    int status = glob(arg, flags, NULL, &pglob);
                                    if (status != 0 && status != 3)
                                    {
                                        // Print error message if globbing fails
                                        fprintf(stderr, "Error: glob() failed with status %d\n", status);
                                    }
                                    else if (status == 3)
                                    {
                                        // Print error message if no matching files found
                                        fprintf(stderr, "No matching files found.\n");
                                    }
                                    // Allocate memory for optarr based on number of matching files
                                    optarr = (char **)realloc(optarr, sizeof(char *) * pglob.gl_pathc + 1);

                                    // Copy each matching file to optarr
                                    for (int i = 0; i < pglob.gl_pathc; i++)
                                    {
                                        if (stat(pglob.gl_pathv[i], &file_stat) != -1)
                                        {
                                            // Finding max option length
                                            if (maxoptlen < strlen(pglob.gl_pathv[i]) + 1)
                                            {
                                                maxoptlen = strlen(pglob.gl_pathv[i]) + 1;
                                            }

                                            // Allocating memory for optarr
                                            optarr[optcnt] = (char *)malloc(sizeof(char) * (maxoptlen));

                                            // Copying the argument to optarr
                                            strcpy(optarr[optcnt], pglob.gl_pathv[i]);
                                            optcnt++;
                                        }
                                    }
                                }
                                else
                                {
                                    printf("cannot access %s: No such flle or directory\n", arg);
                                }
                            }
                        }
                        // Initializing file and folder arrays according to options
                        if (argc == optind)
                        {
                            if (a_flag && l_flag)
                            {
                                list_directory(isBackward, stream, ".", 1, h_flag, S_flag, r_flag);
                            }
                            else if (a_flag)
                            {
                                printAllOption(stream, ".", 1);
                            }
                            else if (l_flag)
                            {
                                list_directory(isBackward, stream, ".", 0, h_flag, S_flag, r_flag);
                            }
                            else
                            {
                                printAllOption(stream, ".", 0);
                            }
                        }
                        else if (optcnt > 0)
                        {
                            for (int i = 0; i < optcnt; i++)
                            {
                                // Checking if optarr[i] is a file or a folder
                                if (stat(optarr[i], &file_stat) == -1)
                                {
                                    perror("stat");
                                    printf("error dir:%s\n", optarr[i]);
                                }
                                if (S_ISREG(file_stat.st_mode))
                                {
                                    filecnt++;
                                }
                                else
                                {
                                    foldercnt++;
                                }
                            }

                            // Allocating memory for filearray and folderarray
                            if (foldercnt > 0)
                            {
                                folderarray = (char **)malloc(sizeof(char *) * foldercnt);
                                for (int i = 0; i < foldercnt; i++)
                                {
                                    folderarray[i] = (char *)malloc(sizeof(char) * maxoptlen);
                                }
                            }
                            if (filecnt > 0)
                            {
                                filearray = (char **)malloc(sizeof(char *) * filecnt);
                                for (int i = 0; i < filecnt; i++)
                                {
                                    filearray[i] = (char *)malloc(sizeof(char) * maxoptlen);
                                }
                            }

                            // Storing files and folders in respective arrays
                            int filecur = 0, foldercur = 0;
                            for (int i = 0; i < optcnt; i++)
                            {
                                if (stat(optarr[i], &file_stat) == -1)
                                {
                                    perror("stat");
                                }
                                if (S_ISREG(file_stat.st_mode))
                                {
                                    strcpy(filearray[filecur], optarr[i]);
                                    filecur++;
                                }
                                else
                                {
                                    strcpy(folderarray[foldercur], optarr[i]);
                                    foldercur++;
                                }
                            }

                            // Sorting the arrays alphabetically
                            bubbleSort(folderarray, foldercnt, maxoptlen);
                            bubbleSort(filearray, filecnt, maxoptlen);

                            // Printing the output according to options
                            optNotEqualPrint(isBackward, stream, a_flag, l_flag, foldercnt, filecnt, folderarray, filearray, optcnt, h_flag, S_flag, r_flag);
                        }
                    }
                    fclose(stream);
                    // time
                    cur_time = time(NULL);
                    c_time = ctime(&(cur_time));
                    c_time[strlen(c_time) - 1] = '\0';

                    gettimeofday(&end, NULL);

                                long long elapsed_time = (end.tv_sec - start.tv_sec) * 1000000LL + (end.tv_usec - start.tv_usec);
                                printf("\n====== Disconnected Client =======\n");
                                printf("TIME : [%s]\n", c_time);
                                printf("URL : %s\n",url);
                                printf("IP: %s\n", new_client.ip);
                                printf("Port: %d\n", new_client.port);
                                printf("CONNECTING TIME: %lld \n", elapsed_time);
                                printf("==================================\n");


                    memset(buffer_thread, 0, sizeof(buffer_thread));
                     offset = 0;

                    offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "\n====== Disconnected Client =======\n");
                    offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "TIME : [%s]\n", c_time);
                    offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "URL : %s\n",url);
                    offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "IP: %s\n", new_client.ip);
                    offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "Port: %d\n", new_client.port);
                    offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "CONNECTING TIME: %lld \n", elapsed_time);
                    offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "==================================\n");

                    pthread_t disconnect_fast;
                    pthread_create(&disconnect_fast, NULL, add_write, buffer_thread);
                    pthread_join(disconnect_fast, NULL);

                    if (write(client_fd, buffer, (buffer_size)) == -1)
                    {
                        perror("write error");
                    };

                    close(client_fd);
                    sleep(5);

                    addIdleProcess(childpid);
                    removeNonIdleCount();
                }
                else if ((is_accessible(inet_ntoa(client_addr.sin_addr))) == 0)
                {
                    // Send HTTP response header
                    fprintf(stream,
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: text/html; charset=UTF-8\r\n\r\n");

                    // Start HTML structure
                    fprintf(stream, "<html>\n<head>\n");
                    fprintf(stream, "<link rel=\"icon\" href=\"data:,\">\n");
                    fprintf(stream, "<title>Access Denied</title>\n");
                    fprintf(stream, "</head>\n<body>\n");
                    // Content
                    fprintf(stream, "<h1>Access Denied!</h1>\n");
                    fprintf(stream, "<p>Your IP: %s</p>\n", inet_ntoa(client_addr.sin_addr));
                    fprintf(stream, "<p>You have no permission to access this web server.</p>\n");
                    fprintf(stream, "<p>HTTP 403.6 - Forbidden: IP address reject</p>\n");

                    // End HTML structure
                    fprintf(stream, "</body>\n</html>\n");
                    fclose(stream);
                    write(client_fd, buffer, (buffer_size));
                    close(client_fd);
                }
            }
        }
        else
        {
            close(client_fd);
            continue;
        }
    }

    while (1)
    {
        if (isForkNeeded())
        {
            while (get_idle_process_count() != 5)
            {
                int client_count = 0;
                if ((pid = fork()) > 0)
                {
                    printf("[%s] %ld process is forked.\n", c_time, (long)pid);
                    sprintf(buffer_thread, "[%s] %d process is forked.\n", c_time, pid);

                    pthread_t add_parent_fork;
                    pthread_create(&add_parent_fork, NULL, add_write, buffer_thread);
                    pthread_join(add_parent_fork, NULL);
                    addIdleProcess(pid);
                }
                else if (pid == 0)
                {
                    pid_t childpid = getpid();
                    // Handle incoming connections
                    signal(SIGUSR1, print_history);
                    signal(SIGTERM, term_exit);
                    signal(SIGINT, SIG_IGN);
                    while (1)
                    {
                        // Declare variables
                        struct in_addr inet_client_address;
                        char *buffer = NULL;
                        size_t buffer_size = 0;
                        FILE *stream = open_memstream(&buffer, &buffer_size);
                        char buf[BUFFSIZE] = {
                            0,
                        };
                        char tmp[BUFFSIZE] = {
                            0,
                        };
                        char *url = NULL;
                        char method[BUFFSIZE] = {
                            0,
                        };
                        char *tok = NULL;

                        char tempbuf[BUFFSIZE] = {
                            0,
                        };
                        // Accept a new connection
                        len = sizeof(client_addr);
                        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &len);

                        read(client_fd, buf, BUFFSIZE);
                        strcpy(tempbuf, buf);
                        char *testok = strtok(tempbuf, " ");

                        // If accept fails, skip this iteration
                        if (client_fd < 0)
                        {
                            continue;
                        }

                        // If IP is accessible, create and set up a new client
                        if (is_accessible(inet_ntoa(client_addr.sin_addr)) && testok != NULL)
                        {
                            // time
                            cur_time = time(NULL);
                            c_time = ctime(&(cur_time));
                            c_time[strlen(c_time) - 1] = '\0';

                            client new_client;
                            new_client.no = shared_memory->historycount + 1;
                            strcpy(new_client.ip, inet_ntoa(client_addr.sin_addr));
                            new_client.port = ntohs(client_addr.sin_port);
                            new_client.pid = getpid();
                            new_client.time = cur_time;

                            // Add new client to shared memory history
                            add_history(new_client);

                            // Get client IP address and print a message
                            inet_client_address.s_addr = client_addr.sin_addr.s_addr;

                            // Read client request

                            // Declare variables
                            char curdir[10000];
                            int isBackward = 0;

                            // Get current working directory
                            if (getcwd(curdir, sizeof(curdir)) == NULL)
                            {
                                perror("getcwd() error");
                            }

                            // Copy request to temporary buffer
                            strcpy(tmp, buf);

                            // Parse request method and URL
                            tok = strtok(tmp, " ");
                            strcpy(method, tok);
                            if (strcmp(method, "GET") == 0)
                            {
                                tok = strtok(NULL, " ");

                                url = extractNonHidden(tok);
                                if (*(url + strlen(url) - 1) == '/')
                                {
                                    *(url + strlen(url) - 1) = '\0';
                                    isBackward = 1;
                                }
                            }

                            // Create a title for the HTML response
                            int len = strlen(inet_ntoa(inet_client_address)) + strlen("40000") + strlen(url) + 2;
                            char *title = (char *)malloc(len * sizeof(char));

                            snprintf(title, len, "%s:%s%s", inet_ntoa(inet_client_address), "40000", url);

                            if (url[0] == '\0') // if the requested URL is empty
                            {
                                url = "."; // set it to the current directory
                            }

                            printf("\n========== New Client ==========\n");
                            printf("[%s]\n", c_time);
                            printf("IP: %s\n", new_client.ip);
                            printf("Port: %d\n", new_client.port);
                            printf("================================\n");

                            char buffer_thread[1024];
                            int offset = 0;

                            offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "\n========== New Client ==========\n");
                            offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "[%s]\n", c_time);
                            offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "IP: %s\n", new_client.ip);
                            offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "Port: %d\n", new_client.port);
                            offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "================================\n");
                            struct timeval start, end;
                            gettimeofday(&start, NULL); // 연결 시작 시간 기록

                            pthread_t add_parent_fork;
                            pthread_create(&add_parent_fork, NULL, add_write, buffer_thread);
                            pthread_join(add_parent_fork, NULL);

                            removeIdleProcess(childpid);
                            addNonIdleCount();

                            if (lstat(url, &checkstat) == -1) // check the status of the requested URL
                            {
                                // perror("404 Not Found\n"); // print an error message
                                fprintf(stream,
                                        "HTTP/1.1 200 OK\r\n"
                                        "Content-Type: text/html; charset=UTF-8\r\n\r\n");
                                fprintf(stream, "<html>\n<head>\n");
                                fprintf(stream, "<link rel=\"icon\" href=\"data:,\">\n"); // add a favicon
                                fprintf(stream, "<title>%s</title>\n", curdir);           // set the page title to the current directory
                                fprintf(stream, "</head>\n<body>\n");
                                fprintf(stream, "<h1>");
                                fprintf(stream, "Not Found");
                                fprintf(stream, "</h1>");
                                fprintf(stream, "<b>The request URL %s was not found on this server<br></b>", url);
                                fprintf(stream, "<b>HTTP 404 - Not Page Found</b>");
                            }
                            else // if the requested URL exists
                            {
                                if ((strcmp(url, ".")) == 0) // if the requested URL is the current directory
                                {

                                    argc = 3;
                                    argv[0] = "ls";
                                    argv[1] = "-l";
                                    argv[2] = url; // set arguments to list the files in the directory
                                }
                                else // if the requested URL is a file or directory
                                {
                                    if (S_ISDIR(checkstat.st_mode)) // if the requested URL is a directory
                                    {
                                        argc = 3;
                                        argv[0] = "ls";
                                        argv[1] = "-al";
                                        argv[2] = url; // set arguments to list the files in the directory
                                    }
                                    else // if the requested URL is a file
                                    {
                                        char *mimestream_buffer = NULL;
                                        size_t mimestream_buffer_size = 0;
                                        const char *mimetype = get_mimetype(url); // get the MIME type of the requested file
                                        FILE *mimestream = open_memstream(&mimestream_buffer, &mimestream_buffer_size);
                                        // find whether if filename is a symbolic link file
                                        int symlink = 0;
                                        struct stat file_stat;
                                        if (lstat(url, &file_stat) == 0)
                                        {
                                            symlink = S_ISLNK(file_stat.st_mode);
                                        }
                                        if (symlink) // If the file is a symbolic link
                                        {
                                            char target[PATH_MAX];
                                            ssize_t len = readlink(url, target, sizeof(target) - 1);
                                            if (len != -1)
                                            {
                                                target[len] = '\0';
                                                mimetype = get_mimetype(target);
                                                url = target;
                                            }
                                        }
                                        if (mimetype)
                                        {
                                            fprintf(mimestream, "HTTP/1.1 200 OK\n");
                                            fprintf(mimestream, "Content-Type: %s\n\n", mimetype); // set the MIME type header
                                            FILE *file = fopen(url, "rb");
                                            if (file)
                                            {
                                                char buffer[1024];
                                                size_t bytes;
                                                while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0)
                                                {
                                                    fwrite(buffer, 1, bytes, mimestream); // write the file contents to the stream
                                                }
                                                fclose(file);
                                            }
                                            else // if the file cannot be opened
                                            {
                                                fprintf(mimestream, "HTTP/1.1 404 Not Found\n");
                                                fprintf(mimestream, "Content-Type: text/html\n\n");
                                                fprintf(mimestream, "<html><body><h1>404 Not Found</h1></body></html>\n"); // print an error message
                                            }
                                        }
                                        else // if the MIME type is not supported
                                        {
                                            fprintf(mimestream, "HTTP/1.1 Unsupported Media Type\n");
                                            fprintf(mimestream, "Content-Type: text/html\n\n");
                                            fprintf(mimestream, "<html><body><h1>Unsupported Media Type</h1></body></html>\n");
                                        }
                                        fclose(mimestream); // Close mimestream

                                        cur_time = time(NULL);
                                        c_time = ctime(&(cur_time));
                                        c_time[strlen(c_time) - 1] = '\0';

                                        // 연결 종료 시간 기록
                                        gettimeofday(&end, NULL);

                                        long long elapsed_time = (end.tv_sec - start.tv_sec) * 1000000LL + (end.tv_usec - start.tv_usec);
                                        printf("\n====== Disconnected Client =======\n");
                                        printf("[%s]\n", c_time);
                                        printf("IP: %s\n", new_client.ip);
                                        printf("Port: %d\n", new_client.port);
                                        printf("CONNECTING TIME: %lld \n", elapsed_time);
                                        printf("==================================\n");

                                        char buffer_thread[1024];
                                        int offset = 0;

                                        offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "\n====== Disconnected Client =======\n");
                                        offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "[%s]\n", c_time);
                                        offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "IP: %s\n", new_client.ip);
                                        offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "Port: %d\n", new_client.port);
                                        offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "CONNECTING TIME: %lld \n", elapsed_time);
                                        offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "==================================\n");

                                        pthread_t add_parent_fork;
                                        pthread_create(&add_parent_fork, NULL, add_write, buffer_thread);
                                        pthread_join(add_parent_fork, NULL);

                                        // Write mimestream_buffer to client_fd; print error if necessary
                                        if (write(client_fd, mimestream_buffer, (mimestream_buffer_size)) == -1)
                                        {
                                            perror("write error");
                                        };

                                        close(client_fd); // Close client file descriptor
                                        sleep(5);

                                        addIdleProcess(childpid);
                                        removeNonIdleCount();
                                        continue; // Go to next iteration
                                    }
                                }
                                // Send HTTP response header
                                fprintf(stream,
                                        "HTTP/1.1 200 OK\r\n"
                                        "Content-Type: text/html; charset=UTF-8\r\n\r\n");

                                // Start HTML structure
                                fprintf(stream, "<html>\n<head>\n");
                                fprintf(stream, "<link rel=\"icon\" href=\"data:,\">\n");
                                fprintf(stream, "<title>%s</title>\n", curdir);
                                fprintf(stream, "</head>\n<body>\n");
                                fprintf(stream, "<h1>");
                                if ((strcmp(url, ".")) == 0) // if the requested URL is the current directory
                                {
                                    fprintf(stream, "Welcome to System Programming Http");
                                }
                                else
                                {
                                    fprintf(stream, "System Programming Http");
                                }
                                fprintf(stream, "</h1>");

                                // Declaring variables

                                int a_flag = 0;
                                int l_flag = 0;
                                int h_flag = 0, S_flag = 0, r_flag = 0;
                                struct stat file_stat;
                                glob_t pglob;
                                // Declaring pointers for storing arguments and options
                                char **optarr;
                                char **folderarray;
                                char **filearray;

                                // Initializing variables
                                int optcnt = 0;
                                int maxoptlen = 0;
                                int filecnt = 0;
                                int foldercnt = 0;

                                // Allocating memory for optarr
                                optarr = (char **)malloc(sizeof(char *) * argc);
                                optind = 1;

                                // Parsing command line options using getopt()
                                while ((opt = getopt(argc, argv, "alhSr")) != -1)
                                {
                                    switch (opt)
                                    {
                                    case 'a': // If -a option is provided
                                        a_flag = 1;
                                        break;
                                    case 'l': // If -l option is provided
                                        l_flag = 1;
                                        break;
                                    case 'h': // If -h option is provided
                                        h_flag = 1;
                                        break;
                                    case 'S': // If -S option is provided
                                        S_flag = 1;
                                        break;
                                    case 'r': // If -r option is provided
                                        r_flag = 1;
                                        break;

                                    default: // If invalid option is detected
                                        printf("Invalid option detected\n");
                                    }
                                }

                                // Storing non-option arguments in optarr
                                for (int i = optind; i < argc; i++)
                                {
                                    char *arg = argv[i];
                                    if (arg != NULL && (lstat(arg, &file_stat) != -1))
                                    {
                                        // Finding max option length
                                        if (maxoptlen < strlen(arg) + 1)
                                        {
                                            maxoptlen = strlen(arg) + 1;
                                        }

                                        // Allocating memory for optarr
                                        optarr[optcnt] = (char *)malloc(sizeof(char) * (maxoptlen));

                                        // Copying the argument to optarr
                                        strcpy(optarr[optcnt], arg);
                                        optcnt++;
                                    }
                                    // If argument is not an option and is not a valid file path
                                    else if (arg[0] != '-' && (access(arg, F_OK) != 0))
                                    {
                                        // If argument contains wildcard characters
                                        if (strchr(arg, '*') != NULL || strchr(arg, '?') != NULL || strchr(arg, '[') != NULL || strchr(arg, ']') != NULL)
                                        {
                                            // Set flags for globbing
                                            int flags;
                                            flags = GLOB_MARK | GLOB_TILDE;

                                            // Perform globbing to get list of matching files
                                            int status = glob(arg, flags, NULL, &pglob);
                                            if (status != 0 && status != 3)
                                            {
                                                // Print error message if globbing fails
                                                fprintf(stderr, "Error: glob() failed with status %d\n", status);
                                            }
                                            else if (status == 3)
                                            {
                                                // Print error message if no matching files found
                                                fprintf(stderr, "No matching files found.\n");
                                            }
                                            // Allocate memory for optarr based on number of matching files
                                            optarr = (char **)realloc(optarr, sizeof(char *) * pglob.gl_pathc + 1);

                                            // Copy each matching file to optarr
                                            for (int i = 0; i < pglob.gl_pathc; i++)
                                            {
                                                if (stat(pglob.gl_pathv[i], &file_stat) != -1)
                                                {
                                                    // Finding max option length
                                                    if (maxoptlen < strlen(pglob.gl_pathv[i]) + 1)
                                                    {
                                                        maxoptlen = strlen(pglob.gl_pathv[i]) + 1;
                                                    }

                                                    // Allocating memory for optarr
                                                    optarr[optcnt] = (char *)malloc(sizeof(char) * (maxoptlen));

                                                    // Copying the argument to optarr
                                                    strcpy(optarr[optcnt], pglob.gl_pathv[i]);
                                                    optcnt++;
                                                }
                                            }
                                        }
                                        else
                                        {
                                            printf("cannot access %s: No such flle or directory\n", arg);
                                        }
                                    }
                                }
                                // Initializing file and folder arrays according to options
                                if (argc == optind)
                                {
                                    if (a_flag && l_flag)
                                    {
                                        list_directory(isBackward, stream, ".", 1, h_flag, S_flag, r_flag);
                                    }
                                    else if (a_flag)
                                    {
                                        printAllOption(stream, ".", 1);
                                    }
                                    else if (l_flag)
                                    {
                                        list_directory(isBackward, stream, ".", 0, h_flag, S_flag, r_flag);
                                    }
                                    else
                                    {
                                        printAllOption(stream, ".", 0);
                                    }
                                }
                                else if (optcnt > 0)
                                {
                                    for (int i = 0; i < optcnt; i++)
                                    {
                                        // Checking if optarr[i] is a file or a folder
                                        if (stat(optarr[i], &file_stat) == -1)
                                        {
                                            perror("stat");
                                            printf("error dir:%s\n", optarr[i]);
                                        }
                                        if (S_ISREG(file_stat.st_mode))
                                        {
                                            filecnt++;
                                        }
                                        else
                                        {
                                            foldercnt++;
                                        }
                                    }

                                    // Allocating memory for filearray and folderarray
                                    if (foldercnt > 0)
                                    {
                                        folderarray = (char **)malloc(sizeof(char *) * foldercnt);
                                        for (int i = 0; i < foldercnt; i++)
                                        {
                                            folderarray[i] = (char *)malloc(sizeof(char) * maxoptlen);
                                        }
                                    }
                                    if (filecnt > 0)
                                    {
                                        filearray = (char **)malloc(sizeof(char *) * filecnt);
                                        for (int i = 0; i < filecnt; i++)
                                        {
                                            filearray[i] = (char *)malloc(sizeof(char) * maxoptlen);
                                        }
                                    }

                                    // Storing files and folders in respective arrays
                                    int filecur = 0, foldercur = 0;
                                    for (int i = 0; i < optcnt; i++)
                                    {
                                        if (stat(optarr[i], &file_stat) == -1)
                                        {
                                            perror("stat");
                                        }
                                        if (S_ISREG(file_stat.st_mode))
                                        {
                                            strcpy(filearray[filecur], optarr[i]);
                                            filecur++;
                                        }
                                        else
                                        {
                                            strcpy(folderarray[foldercur], optarr[i]);
                                            foldercur++;
                                        }
                                    }

                                    // Sorting the arrays alphabetically
                                    bubbleSort(folderarray, foldercnt, maxoptlen);
                                    bubbleSort(filearray, filecnt, maxoptlen);

                                    // Printing the output according to options
                                    optNotEqualPrint(isBackward, stream, a_flag, l_flag, foldercnt, filecnt, folderarray, filearray, optcnt, h_flag, S_flag, r_flag);
                                }
                            }
                            fclose(stream);
                            // time
                            cur_time = time(NULL);
                            c_time = ctime(&(cur_time));
                            c_time[strlen(c_time) - 1] = '\0';

                            gettimeofday(&end, NULL);

                            long long elapsed_time = (end.tv_sec - start.tv_sec) * 1000000LL + (end.tv_usec - start.tv_usec);
                            printf("\n====== Disconnected Client =======\n");
                            printf("[%s]\n", c_time);
                            printf("IP: %s\n", new_client.ip);
                            printf("Port: %d\n", new_client.port);
                            printf("CONNECTING TIME: %lld \n", elapsed_time);
                            printf("==================================\n");

                            memset(buffer_thread, 0, sizeof(buffer_thread));
                            offset = 0;

                            offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "\n====== Disconnected Client =======\n");
                            offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "[%s]\n", c_time);
                            offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "IP: %s\n", new_client.ip);
                            offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "Port: %d\n", new_client.port);
                            offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "CONNECTING TIME: %lld \n", elapsed_time);
                            offset += snprintf(buffer_thread + offset, sizeof(buffer_thread) - offset, "==================================\n");

                            pthread_t disconnect_fast;
                            pthread_create(&disconnect_fast, NULL, add_write, buffer_thread);
                            pthread_join(disconnect_fast, NULL);

                            if (write(client_fd, buffer, (buffer_size)) == -1)
                            {
                                perror("write error");
                            };

                            close(client_fd);
                            sleep(5);

                            addIdleProcess(childpid);
                            removeNonIdleCount();
                        }
                        else if ((is_accessible(inet_ntoa(client_addr.sin_addr))) == 0)
                        {
                            // Send HTTP response header
                            fprintf(stream,
                                    "HTTP/1.1 200 OK\r\n"
                                    "Content-Type: text/html; charset=UTF-8\r\n\r\n");

                            // Start HTML structure
                            fprintf(stream, "<html>\n<head>\n");
                            fprintf(stream, "<link rel=\"icon\" href=\"data:,\">\n");
                            fprintf(stream, "<title>Access Denied</title>\n");
                            fprintf(stream, "</head>\n<body>\n");
                            // Content
                            fprintf(stream, "<h1>Access Denied!</h1>\n");
                            fprintf(stream, "<p>Your IP: %s</p>\n", inet_ntoa(client_addr.sin_addr));
                            fprintf(stream, "<p>You have no permission to access this web server.</p>\n");
                            fprintf(stream, "<p>HTTP 403.6 - Forbidden: IP address reject</p>\n");

                            // End HTML structure
                            fprintf(stream, "</body>\n</html>\n");
                            fclose(stream);
                            write(client_fd, buffer, (buffer_size));
                            close(client_fd);
                        }
                    }
                }
                else
                {
                    close(client_fd);
                    continue;
                }
            }
        }
    }

    for (;;)
        pause();
    sem_unlink("semaphore");
    sem_close(semaphore);
    cleanup_shared_memory();
    pthread_mutex_destroy(&mutex);
    close(server_fd);
    return 0;
}

///////////////////////////////////////////////////////////////////////
// get_mimetype                                                      //
// ================================================================= //
// Input: const char filename                                        //
// -> The filename for which the MIME type needs to be determined    //
// Output: const char *                                              //
// -> The MIME type string corresponding to the file type            //
// Purpose: Determine the MIME type of a given file based on its     //
// filename extension                                                //
///////////////////////////////////////////////////////////////////////
const char *get_mimetype(const char *filename)
{ // using fnmatch to match MIME type(html,txt,imagefiles)
    struct stat file_stat;
    if (stat(filename, &file_stat) == 0)
    {
        if (S_ISREG(file_stat.st_mode) && (file_stat.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
        {
            return "application/octet-stream";
        }
    }
    if (fnmatch("*.jpg", filename, FNM_CASEFOLD) == 0 || fnmatch("*.jpeg", filename, FNM_CASEFOLD) == 0 || fnmatch("*.png", filename, FNM_CASEFOLD) == 0)
    {
        return "image/*";
    }
    else
    {
        return "text/plain";
    }
}

///////////////////////////////////////////////////////////////////////
// optNotEqualPrint                                                  //
// ================================================================= //
// Input: int isBackward, FILE *stream, int a_flag, int l_flag,      //
// int foldercnt, int filecnt, char **folderarray,                   //
// char **filearray, int optcnt, int isHflag, int isSflag,           //
// int isRflag                                                       //
// -> int isBackward: Backward option flag                           //
// -> FILE *stream: File pointer for the HTML file                   //
// -> int a_flag: Flag indicating whether or not to show hidden files//
// -> int l_flag: Flag indicating whether or not to show detailed    //
// file information                                                  //
// -> int foldercnt: Number of folders to process                    //
// -> int filecnt: Number of files to process                        //
// -> char **folderarray: 2D char array that stores the names of the //
// folders                                                           //
// -> char **filearray: 2D char array that stores the names of the   //
// files                                                             //
// -> int optcnt: Total number of options to process                 //
// -> int isHflag: H option flag                                     //
// -> int isSflag: S option flag                                     //
// -> int isRflag: R option flag                                     //
// (Input parameter Description)                                     //
// Output: void                                                      //
// Purpose: Print information about files and folders based on       //
// the specified options.                                            //
///////////////////////////////////////////////////////////////////////
void optNotEqualPrint(int isBackward, FILE *stream, int a_flag, int l_flag, int foldercnt, int filecnt, char **folderarray, char **filearray, int optcnt, int isHflag, int isSflag, int isRflag)
{

    // Set up counters to track current file and folder being worked on
    int filecur = 0, foldercur = 0;

    char curdir[10000];
    if (getcwd(curdir, sizeof(curdir)) == NULL)
    {
        perror("getcwd() error");
    }
    // If both -a and -l flags are set
    if (a_flag && l_flag)
    {
        // Loop through all options
        for (int i = 0; i < optcnt; i++)
        {
            // If there are files left
            if (filecnt != 0)
            {
                // Print detailed information about the file using printLFiles
                printLFiles(stream, filearray[filecur], isHflag, isSflag, isRflag);

                // Increment filecur and decrement filecnt
                filecur++;
                filecnt--;
            }
            // If there are no more files but there are folders left
            else if (foldercnt != 0)
            {
                // List contents of the directory using list_directory
                list_directory(isBackward, stream, folderarray[foldercur], 1, isHflag, isSflag, isRflag);

                // Increment foldercur and decrement foldercnt
                foldercur++;
                foldercnt--;
            }
        }
    }
    // If only -a flag is set
    else if (a_flag)
    {
        if (filecnt != 0)
        {
            fprintf(stream, "<b>Directory path: %s</b>", ".");
            fprintf(stream, "<table border=\"1\">\n");
            fprintf(stream, "<tr><th>Name</th></tr>\n");
            // Loop through all options
            while (filecnt != 0)
            {
                // If there are files left
                // Print name of the file
                printAllOption(stream, filearray[filecur], 1);

                // Increment filecur and decrement filecnt
                filecur++;
                filecnt--;
            }
            fprintf(stream, "</table>\n"); // </table> 태그를 추가
        }
        // Loop through all options
        for (int i = 0; i < optcnt; i++)
        {
            // If there are no more files but there are folders left
            if (foldercnt != 0)
            {
                // Print basic information about all files in the directory using printAllOption
                printAllOption(stream, folderarray[foldercur], 1);

                // Increment foldercur and decrement foldercnt
                foldercur++;
                foldercnt--;
            }
        }
    }
    // If only -l flag is set
    else if (l_flag)
    {
        // Loop through all options
        for (int i = 0; i < optcnt; i++)
        {
            // If there are files left
            if (filecnt != 0)
            {
                // Print detailed information about the file using printLFiles
                printLFiles(stream, filearray[filecur], isHflag, isSflag, isRflag);

                // Increment filecur and decrement filecnt
                filecur++;
                filecnt--;
            }
            // If there are no more files but there are folders left
            else if (foldercnt != 0)
            {
                // List contents of the directory without showing hidden files using list_directory
                list_directory(isBackward, stream, folderarray[foldercur], 0, isHflag, isSflag, isRflag);

                // Increment foldercur and decrement foldercnt
                foldercur++;
                foldercnt--;
            }
        }
    }
    // If neither -a nor -l flags are set
    else
    {
        if (filecnt != 0)
        {
            fprintf(stream, "<b>Directory path: %s</b>", ".");
            fprintf(stream, "<table border=\"1\">\n");
            fprintf(stream, "<tr><th>Name</th></tr>\n");
            // Loop through all options
            while (filecnt != 0)
            {
                // If there are files left
                // Print name of the file
                printAllOption(stream, filearray[filecur], 1);

                // Increment filecur and decrement filecnt
                filecur++;
                filecnt--;
            }
            fprintf(stream, "</table>\n");
        }
        // Loop through all options
        for (int i = 0; i < optcnt; i++)
        {
            // If there are no more files but there are folders left
            if (foldercnt != 0)
            {
                // Print basic information about all files in the directory without showing hidden files by calling printAllOption
                printAllOption(stream, folderarray[foldercur], 0);

                // Increment foldercur and decrement foldercnt
                foldercur++;
                foldercnt--;
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////
// initHeightWeight                                                  //
// ================================================================= //
// Input: int *w, int *h, char *dir                                  //
//*w-> pointer of Array weight,                                      //
//*h-> pointer of Array height,                                      //
//*dir> pointer of directory                                         //
// (Input parameter Description)                                     //
// Output: void                                                      //
// (Out parameter Description)                                       //
// Purpose: initialize Height and Weight of 2D Array                 //
///////////////////////////////////////////////////////////////////////
void initHeightWeight(int *w, int *h, char *dir, int isShowHidden)
{

    DIR *t_dirp;
    struct dirent *t_dir;

    if (dir != NULL)
    {
        t_dirp = opendir(dir); // Open specified directory
    }
    else
    {
        t_dirp = opendir("."); // Open current directory
    }
    if (isShowHidden)
    {
        while ((t_dir = readdir(t_dirp)) != NULL)
        {
            (*h)++;
            (*w) = (strlen(t_dir->d_name) > (*w)) ? strlen(t_dir->d_name) : (*w); // Update max width
        }
    }
    else
    {
        while ((t_dir = readdir(t_dirp)) != NULL)
        {
            if (t_dir->d_name[0] != '.') // Skip hidden files
            {
                (*h)++;
                (*w) = (strlen(t_dir->d_name) > (*w)) ? strlen(t_dir->d_name) : (*w); // Update max width
            }
        }
    }
    (*w)++;           // Add space for null character
    closedir(t_dirp); // Close directory
}

///////////////////////////////////////////////////////////////////////
// BubbleSort                                                        //
// ================================================================= //
// Input: char *str[], int height, int weight                        //
//-> Insert 2D char Array,height of Array,weight of Array            //
// char Array -> 2Dimensional char Array that store filename         //
// height of Array -> height of Array                                //
// weight of Array -> weithgt of Array                               //
// (Input parameter Description)                                     //
// Output: void                                                      //
// (Out parameter Description)                                       //
// Purpose: Bubble Sorting Array                                     //
///////////////////////////////////////////////////////////////////////
void bubbleSort(char *str[], int height, int weight)
{
    char *temp_str = (char *)malloc(sizeof(char) * weight);
    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < height - i - 1; j++)
        {

            if (str[j][0] != '.' && str[j + 1][0] == '.')
            {
                char *com_arr = extractNonHidden(str[j + 1]);
                if ((strcasecmp(str[j], com_arr) > 0)) // Compare strings case-insensitive
                {
                    strcpy(temp_str, str[j]); // Swap strings
                    strcpy(str[j], str[j + 1]);
                    strcpy(str[j + 1], temp_str);
                }
            }
            else if (str[j][0] == '.' && str[j + 1][0] != '.')
            {
                char *com_arr = extractNonHidden(str[j]);
                if ((strcasecmp(com_arr, str[j + 1]) > 0)) // Compare strings case-insensitive
                {
                    strcpy(temp_str, str[j]); // Swap strings
                    strcpy(str[j], str[j + 1]);
                    strcpy(str[j + 1], temp_str);
                }
            }
            else
            {
                if ((strcasecmp(str[j], str[j + 1]) > 0)) // Compare strings case-insensitive
                {
                    strcpy(temp_str, str[j]); // Swap strings
                    strcpy(str[j], str[j + 1]);
                    strcpy(str[j + 1], temp_str);
                }
            }
        }
    }
    free(temp_str);
}

///////////////////////////////////////////////////////////////////////
// extractNonHidden                                                  //
// ================================================================= //
// Input: char *origin                                               //
//-> String that needs to remove the first character                 //
// (Input parameter Description)                                     //
// Output: char *                                                    //
// -> Modified string without the first character                    //
// (Out parameter Description)                                       //
// Purpose: Remove the first character of a string                   //
///////////////////////////////////////////////////////////////////////
char *extractNonHidden(char *origin)
{
    // Allocate memory for the new string, which will have the same length as `origin` but
    char *newone = (char *)malloc(sizeof(char) * strlen(origin) + 1);

    // Create a pointer `p` that points to the beginning of the new string
    char *p = newone;

    // Loop through each character in `origin`, skipping the first one
    while (*origin != '\0')
    {
        origin++;          // move the pointer forward by one character
        *newone = *origin; // copy the current character to the new string
        newone++;          // move the pointer for the new string forward
    }

    // Add a null terminator to the end of the new string
    *newone = '\0';

    // Return a pointer to the beginning of the new string
    return p;
}

///////////////////////////////////////////////////////////////////////
// extractRelativeDir                                                //
// ================================================================= //
// Input: char *origin                                               //
// -> Original string representing the directory path                //
// Output: char *                                                    //
// -> Modified string with the relative directory path extracted     //
// Purpose: Extract the relative directory path from the given path  //
///////////////////////////////////////////////////////////////////////
char *extractRelativeDir(char *origin)
{
    // Allocate memory for the new string, which will have the same length as `origin` but
    char *newone = (char *)malloc(sizeof(char) * strlen(origin) + 1);

    // Create a pointer `p` that points to the beginning of the new string
    char *p = newone;

    // Loop through each character in `origin`, skipping the first one
    while (*origin != '\0')
    {
        origin++; // move the pointer forward by one character
    }
    origin--;
    while (*origin != '/')
    {
        *newone = *origin;
        newone++;
        origin--;
    }

    // Add a null terminator to the end of the new string
    *newone = '\0';

    // Reverse the new string
    char *begin = p;
    char *end = newone - 1;
    char temp;

    while (begin < end)
    {
        temp = *begin;
        *begin = *end;
        *end = temp;

        begin++;
        end--;
    }

    // Return a pointer to the beginning of the new string
    return p;
}

///////////////////////////////////////////////////////////////////////
// list_directory                                                    //
// ================================================================= //
// Input: int isBackward, FILE *stream, char *dir_path,              //
// int isShowHidden, int isHflag, int isSflag, int isRflag           //
// -> int isBackward: Backward option flag                           //
// -> FILE *stream: File pointer for the HTML file                   //
// -> char *dir_path: Path of the directory to list                  //
// -> int isShowHidden: Whether to show hidden files                 //
// -> int isHflag: H option flag                                     //
// -> int isSflag: S option flag                                     //
// -> int isRflag: R option flag                                     //
// (Input parameter Description)                                     //
// Output: void                                                      //
// (Out parameter Description)                                       //
// Purpose: Lists the contents of a directory                        //
///////////////////////////////////////////////////////////////////////
void list_directory(int isBackward, FILE *stream, char *dir_path, int isShowHidden, int isHflag, int isSflag, int isRflag)
{
    DIR *dir;               // Directory pointer
    struct dirent *entry;   // Directory entry pointer
    struct stat total_stat; // Struct that contains information about the entries(files/directories) in the directory
    struct stat file_stat;  // Struct that will store information about an individual file
    struct passwd *pwd;     // Struct that will contain file owner's username and uid
    struct group *grp;      // Struct that will contain file owner's group name and gid
    char date_str[20];      // Buffer that will store the converted modification time of the file
    int total_size = 0;

    char prev_dir[1000000];
    char *dirto = NULL;
    char *showdir = NULL;

    dirto = dir_path;
    // Get the current working directory
    if (getcwd(prev_dir, sizeof(prev_dir)) == NULL)
    {
        perror("Failed to get the current working directory");
    }
    else
    {
        if (dir_path[0] == '.' && dir_path[1] == '\0')
        {
            showdir = (char *)malloc(sizeof(char) * strlen(prev_dir) + 1); // Allocate 2D array
            strcpy(showdir, prev_dir);
        }
        else
        {
            // Calculate the required memory size
            int len = strlen(prev_dir) + strlen(dir_path) + 2;
            showdir = (char *)malloc(len * sizeof(char));
            // Create a string
            snprintf(showdir, len, "%s/%s", prev_dir, dir_path);
        }
        // Print the directory path
    }

    int w = 0, h = 0;
    char **filename = NULL;

    // Initialize width and height of 2D array and allocate memory to it
    initHeightWeight(&w, &h, dirto, isShowHidden);
    filename = (char **)malloc(sizeof(char *) * h);
    for (int i = 0; i < h; i++)
    {
        filename[i] = (char *)malloc(sizeof(char) * w); // Allocate 2D array
    }

    // Try to open the directory
    dir = opendir(dirto);
    if (dir == NULL)
    {
        printf("Unable to open directory");
        return;
    }

    int i = 0;
    if (isShowHidden)
    {
        // Add all files to the 2D array
        while ((entry = readdir(dir)) != NULL)
        {
            strcpy(filename[i], entry->d_name);
            i++;
        }
    }
    else
    {
        // Add non-hidden files to the 2D array
        while ((entry = readdir(dir)) != NULL)
        {

            if (entry->d_name[0] != '.')
            {
                strcpy(filename[i], entry->d_name);
                i++;
            }
        }
    }

    free(entry); // Free memory allocated to the directory entry

    unsigned int *fileSizeArray = NULL;
    fileSizeArray = (unsigned int *)malloc(sizeof(unsigned int) * h);

    // Get total size of all files and directories in the directory
    for (int i = 0; i < h; i++)
    {
        if (strstr(filename[i], "html_ls.html"))
        {

            continue;
        }

        // Calculate the required memory size
        int len = strlen(dirto) + strlen(filename[i]) + 2;
        char *filedir = (char *)malloc(len * sizeof(char));

        // Create a string
        snprintf(filedir, len, "%s/%s", dirto, filename[i]);
        // Retrieve file information
        if (lstat(filedir, &total_stat) == -1)
        {
            perror("lstat");
            free(filedir);
            continue;
        }

        // Save the file size
        total_size += total_stat.st_blocks;
        fileSizeArray[i] = (unsigned int)total_stat.st_size;

        // Free allocated memory
        free(filedir);
    }

    if (isRflag || isSflag)
    {
        sizeBubbleSort(fileSizeArray, filename, h, w, isRflag);
    }
    else
    {
        bubbleSort(filename, h, w); // Sort the filenames in ascending order using bubble sort algorithm
    }

    // Print the total size of all files and directories in the directory
    fprintf(stream, "<b>Directory path: %s</b>", showdir);
    fprintf(stream, "<br><b>total %d</b>", total_size / 2);
    fprintf(stream, "<table border=\"1\">\n");
    fprintf(stream, "<tr><th>Name</th><th>Permission</th><th>Link</th><th>Owner</th><th>Group</th><th>Size</th><th>Last Modified</th></tr>\n");

    // Print the file type, permissions, owner, group, size, modification time, and name of each file in the directory
    for (int i = 0; i < h; i++)
    {
        if (strstr(filename[i], "html_ls.html"))
        {
            continue;
        }
        char *hypedir = NULL;

        if (isBackward)
        {
            hypedir = filename[i];
        }
        else
        {
            if (strchr(dir_path, '/') == NULL)
            {
                int len = strlen(dir_path) + strlen(filename[i]) + 2;
                hypedir = (char *)malloc(len * sizeof(char));
                snprintf(hypedir, len, "%s/%s", dir_path, filename[i]);
            }
            else
            {
                char *rela_hypedir = extractRelativeDir(dir_path);
                int len = strlen(rela_hypedir) + strlen(filename[i]) + 2;
                hypedir = (char *)malloc(len * sizeof(char));
                snprintf(hypedir, len, "%s/%s", rela_hypedir, filename[i]);
            }
        }

        int len = strlen(dirto) + strlen(filename[i]) + 2;
        char *filedir = (char *)malloc(len * sizeof(char));
        snprintf(filedir, len, "%s/%s", dirto, filename[i]);
        if (lstat(filedir, &file_stat) == -1)
        {
            perror("lstat");
            continue;
        }
        // Get file owner and group
        pwd = getpwuid(file_stat.st_uid);
        grp = getgrgid(file_stat.st_gid);

        // Get modified date and time
        strftime(date_str, sizeof(date_str), "%b %d %H:%M", localtime(&file_stat.st_mtime));

        // Print file type, permission, and other details

        if (S_ISLNK(file_stat.st_mode))
        {
            fprintf(stream, "<tr style=\"color: green;\">");
            fprintf(stream, "<td><a href=\"%s\">%s</a></td>\n", hypedir, filename[i]);
            fprintf(stream, "<td>l");
        }
        else if (S_ISDIR(file_stat.st_mode))
        {
            fprintf(stream, "<tr style=\"color: blue;\">");
            fprintf(stream, "<td><a href=\"%s\">%s</a></td>\n", hypedir, filename[i]);
            fprintf(stream, "<td>d");
        }
        else
        {
            fprintf(stream, "<tr style=\"color: red;\">");
            fprintf(stream, "<td><a href=\"%s\">%s</a></td>\n", hypedir, filename[i]);
            fprintf(stream, "<td>-");
        }

        fprintf(stream, (file_stat.st_mode & S_IRUSR) ? "r" : "-");
        fprintf(stream, (file_stat.st_mode & S_IWUSR) ? "w" : "-");
        fprintf(stream, (file_stat.st_mode & S_IXUSR) ? "x" : "-");
        fprintf(stream, (file_stat.st_mode & S_IRGRP) ? "r" : "-");
        fprintf(stream, (file_stat.st_mode & S_IWGRP) ? "w" : "-");
        fprintf(stream, (file_stat.st_mode & S_IXGRP) ? "x" : "-");
        fprintf(stream, (file_stat.st_mode & S_IROTH) ? "r" : "-");
        fprintf(stream, (file_stat.st_mode & S_IWOTH) ? "w" : "-");
        fprintf(stream, (file_stat.st_mode & S_IXOTH) ? "x" : "-");

        // Close </td> tag
        fprintf(stream, "</td>");

        // Print file links, owner, and group
        fprintf(stream, "<td>%ld</td>", file_stat.st_nlink);
        fprintf(stream, "<td>%s</td>", pwd->pw_name);
        fprintf(stream, "<td>%s</td>", grp->gr_name);

        // Print file size based on isHflag condition
        if (isHflag && file_stat.st_size >= 1024 && file_stat.st_size < 1024 * 1024)
        {
            if (file_stat.st_size >= 10240)
            {
                long int filech = file_stat.st_size / 1024;
                fprintf(stream, "<td>%ldK</td>", filech);
            }
            else
            {
                double filech = (double)file_stat.st_size / (double)1024;
                fprintf(stream, "<td>%0.1lfK</td>", filech);
            }
        }
        else if (isHflag && file_stat.st_size >= 1024 * 1024 && file_stat.st_size < 1024 * 1024 * 1024)
        {
            long int filech = file_stat.st_size / (1024 * 1024);
            fprintf(stream, "<td>%ldM</td>", filech);
        }
        else if (isHflag && file_stat.st_size >= 1024 * 1024 * 1024)
        {
            long int filech = file_stat.st_size / (1024 * 1024 * 1024);
            fprintf(stream, "<td>%ldG</td>", filech);
        }
        else
        {
            fprintf(stream, "<td>%ld</td>", file_stat.st_size);
        }

        // Print modification date and close table row
        fprintf(stream, "<td>%s</td>", date_str);
        fprintf(stream, "</tr>\n");

        // Free filedir memory
        free(filedir);
    }
    // Close table tag
    fprintf(stream, "</table>\n");

    // Free the memory allocated to the 2D array and close the directory
    for (int i = 0; i < h; i++)
    {
        free(filename[i]);
    }
    free(filename);
    closedir(dir);

    // Move back to the previous directory
    // chdir(prev_dir);

    printf("\n");
}

///////////////////////////////////////////////////////////////////////
// printLFiles                                                       //
// ================================================================= //
// Input: char *filename                                             //
// ->  FILE *stream: File pointer htmlfile                          //
// -> int isHflag: H option flag                                     //
// -> int isSflag: S option flag                                     //
// -> int isRflag: R option flag                                     //
// -> File name to print the information of                          //
// filename -> Name of the file to print information of              //
// (Input parameter Description)                                     //
// Output: void                                                      //
// (Out parameter Description)                                       //
// Purpose: Prints the details of a file in the long format (ls -l)  //
///////////////////////////////////////////////////////////////////////
void printLFiles(FILE *stream, char *filename, int isHflag, int isSflag, int isRflag)
{

    struct stat file_stat;
    struct passwd *pwd;
    struct group *grp;
    char date_str[20];
    char curdir[100000];
    char *dirto = NULL;
    char *filedir = NULL;

    if (strstr(filename, "html_ls.html"))
        return;

    if (lstat(filename, &file_stat) == -1)
    {
        perror("lstat");
    }

    pwd = getpwuid(file_stat.st_uid);
    grp = getgrgid(file_stat.st_gid);

    // Get modified date and time
    strftime(date_str, sizeof(date_str), "%b %d %H:%M", localtime(&file_stat.st_mtime));
    fprintf(stream, "<b>Directory path: %s</b>", filename);
    // fprintf(stream, "<h1>%s</h1>\n", filename);
    fprintf(stream, "<table border=\"1\">\n");
    fprintf(stream, "<tr><th>Name</th><th>Permission</th><th>Link</th><th>Owner</th><th>Group</th><th>Size</th><th>Last Modified</th></tr>\n");

    // Print file type, permission, and other details
    if (S_ISLNK(file_stat.st_mode))
    {
        fprintf(stream, "<tr style=\"color: green;\">");
        if (filename[0] != '/')
        {
            dirto = getcwd(curdir, sizeof(curdir));
            int len = strlen(dirto) + strlen(filename) + 2;
            filedir = (char *)malloc(len * sizeof(char));
            snprintf(filedir, len, "%s/%s", dirto, filename);
            fprintf(stream, "<td><a href=\"%s\">%s</a></td>\n", filename, filename);
        }
        else
        {
            dirto = filename;
            fprintf(stream, "<td><a href=\"%s\">%s</a></td>\n", filename, filename);
        }
        fprintf(stream, "<td>l");
    }
    else if (S_ISDIR(file_stat.st_mode))
    {
        fprintf(stream, "<tr style=\"color: blue;\">");
        if (filename[0] != '/')
        {
            dirto = getcwd(curdir, sizeof(curdir));
            int len = strlen(dirto) + strlen(filename) + 2;
            filedir = (char *)malloc(len * sizeof(char));
            snprintf(filedir, len, "%s/%s", dirto, filename);
            fprintf(stream, "<td><a href=\"%s\">%s</a></td>\n", filename, filename);
        }
        else
        {
            dirto = filename;
            fprintf(stream, "<td><a href=\"%s\">%s</a></td>\n", filename, filename);
        }
        fprintf(stream, "<td>d");
    }
    else
    {
        fprintf(stream, "<tr style=\"color: red;\">");
        if (filename[0] != '/')
        {
            dirto = getcwd(curdir, sizeof(curdir));
            int len = strlen(dirto) + strlen(filename) + 2;
            filedir = (char *)malloc(len * sizeof(char));
            snprintf(filedir, len, "%s/%s", dirto, filename);
            fprintf(stream, "<td><a href=\"%s\">%s</a></td>\n", filename, filename);
        }
        else
        {
            dirto = filename;
            fprintf(stream, "<td><a href=\"%s\">%s</a></td>\n", filename, filename);
        }
        fprintf(stream, "<td>-");
    }

    fprintf(stream, (file_stat.st_mode & S_IRUSR) ? "r" : "-");
    fprintf(stream, (file_stat.st_mode & S_IWUSR) ? "w" : "-");
    fprintf(stream, (file_stat.st_mode & S_IXUSR) ? "x" : "-");
    fprintf(stream, (file_stat.st_mode & S_IRGRP) ? "r" : "-");
    fprintf(stream, (file_stat.st_mode & S_IWGRP) ? "w" : "-");
    fprintf(stream, (file_stat.st_mode & S_IXGRP) ? "x" : "-");
    fprintf(stream, (file_stat.st_mode & S_IROTH) ? "r" : "-");
    fprintf(stream, (file_stat.st_mode & S_IWOTH) ? "w" : "-");
    fprintf(stream, (file_stat.st_mode & S_IXOTH) ? "x" : "-");

    fprintf(stream, "</td>");

    fprintf(stream, "<td>%ld</td>", file_stat.st_nlink);
    fprintf(stream, "<td>%s</td>", pwd->pw_name);
    fprintf(stream, "<td>%s</td>", grp->gr_name);

    if (isHflag && file_stat.st_size >= 1024 && file_stat.st_size < 1024 * 1024)
    {
        if (file_stat.st_size >= 10240)
        {
            long int filech = file_stat.st_size / 1024;
            fprintf(stream, "<td>%ldK</td>", filech);
        }
        else
        {
            double filech = (double)file_stat.st_size / (double)1024;
            fprintf(stream, "<td>%0.1lfK</td>", filech);
        }
    }
    else if (isHflag && file_stat.st_size >= 1024 * 1024 && file_stat.st_size < 1024 * 1024 * 1024)
    {
        long int filech = file_stat.st_size / (1024 * 1024);
        fprintf(stream, "<td>%ldM</td>", filech);
    }
    else if (isHflag && file_stat.st_size >= 1024 * 1024 * 1024)
    {
        long int filech = file_stat.st_size / (1024 * 1024 * 1024);
        fprintf(stream, "<td>%ldG</td>", filech);
    }
    else
    {
        fprintf(stream, "<td>%ld</td>", file_stat.st_size);
    }

    fprintf(stream, "<td>%s</td>", date_str);
    fprintf(stream, "</tr>\n");    // </tr> 태그를 닫고 개행 문자를 추가
    fprintf(stream, "</table>\n"); // </table> 태그를 추가
}
///////////////////////////////////////////////////////////////////////
// printAllOption                                                    //
// ================================================================= //
// Input:                                                            //
// ->  FILE *stream: File pointer htmlfile                          //
// -> char *directory: Path of the directory to display files in     //
// -> int isShowHidden: Indicates whether to show hidden files or not//
//                                                                   //
// Output: void                                                      //
// -> This function does not return anything                         //
//                                                                   //
// Purpose: Displays the contents of a directory in alphabetical     //
// order and with an option to display hidden files.                 //
//                                                                   //
///////////////////////////////////////////////////////////////////////
void printAllOption(FILE *stream, char *directory, int isShowHidden)
{
    DIR *dirp;
    struct dirent *dir;
    struct stat file_stat;
    char **filename;
    int w = 0, h = 0;
    char prev_dir[1000000];
    char *dirto = NULL;
    char *filedir = NULL;

    // Get the current working directory
    if (getcwd(prev_dir, sizeof(prev_dir)) == NULL)
    {
        perror("Failed to get the current working directory");
    }
    else
    {
        if (directory == ".")
        {
            dirto = prev_dir;
        }
        else
        {
            dirto = directory;
        }
    }

    if ((dirp = opendir(directory)) == NULL)
    {
        if (lstat(directory, &file_stat) == -1)
        {
            printf("cannot access '%s' : No such directory\n", directory); // Print error
        }
        else
        {
            if (strstr(directory, "html_ls.html"))
                return;
            if (S_ISLNK(file_stat.st_mode))
            {
                fprintf(stream, "<tr style=\"color: green;\">");
            }
            else
            {
                fprintf(stream, "<tr style=\"color: red;\">");
            }
            if (directory[0] != '/')
            {
                dirto = prev_dir;
                int len = strlen(dirto) + strlen(directory) + 2;
                filedir = (char *)malloc(len * sizeof(char));
                snprintf(filedir, len, "%s/%s", dirto, directory);
                fprintf(stream, "<td><a href=\"%s\">%s</a></td></tr>\n", directory, directory);
            }
            else
            {
                dirto = directory;
                fprintf(stream, "<td><a href=\"%s\">%s</a></td>/tr>\n", directory, directory);
            }
        }
    }
    else
    {
        // Print table head
        fprintf(stream, "<b>Directory path: %s</b>", dirto);
        fprintf(stream, "<table border=\"1\">\n");
        fprintf(stream, "<tr><th>Name</th></tr>\n");
        initHeightWeight(&w, &h, directory, isShowHidden);
        filename = (char **)malloc(sizeof(char *) * h); // Allocate 1D array
        for (int i = 0; i < h; i++)
        {
            filename[i] = (char *)malloc(sizeof(char) * w); // Allocate 2D array
        }
        int i = 0;
        if (isShowHidden)
        {
            while ((dir = readdir(dirp)) != NULL)
            {
                strcpy(filename[i], dir->d_name);
                i++;
            }
            closedir(dirp); // Close directory
        }
        else
        {
            while ((dir = readdir(dirp)) != NULL)
            {
                if (dir->d_name[0] != '.') // Skip hidden files
                {
                    strcpy(filename[i], dir->d_name);
                    i++;
                }
            }
            closedir(dirp); // Close directory
        }

        // Sort filenames
        bubbleSort(filename, h, w);

        // Print sorted filenames
        for (int i = 0; i < h; i++)
        {
            if (strstr(filename[i], "html_ls.html"))
                continue;
            int len = strlen(dirto) + strlen(filename[i]) + 2;
            char *filedir = (char *)malloc(len * sizeof(char));

            snprintf(filedir, len, "%s/%s", dirto, filename[i]);
            if (lstat(filedir, &file_stat) == -1)
            {
                perror("lstat");
            }
            if (S_ISLNK(file_stat.st_mode))
            {
                fprintf(stream, "<tr style=\"color: green;\">");
            }
            else if (S_ISDIR(file_stat.st_mode))
            {
                fprintf(stream, "<tr style=\"color: blue;\">");
            }
            else
            {
                fprintf(stream, "<tr style=\"color: red;\">");
            }
            fprintf(stream, "<td><a href=\"%s\">%s</a></td></tr>\n", filename[i], filename[i]);
        }
        fprintf(stream, "</table>\n");
    }

    printf("\n");
}
///////////////////////////////////////////////////////////////////////
// sizeBubbleSort                                                    //
// ================================================================= //
// Input:                                                            //
// -> unsigned int *sizeary: An array of file sizes                  //
// -> char *str[]: An array of file names                            //
// -> int height: Number of files in the directory                   //
// -> int weight: Maximum length of a filename                       //
// -> int isRflag: Indicates whether to sort in reverse order        //
//                                                                   //
// Output: void                                                      //
// -> This function does not return anything                         //
//                                                                   //
// Purpose: Sorts an array of file sizes and corresponding filenames //
// -> This function sorts an array of file sizes and corresponding   //
///////////////////////////////////////////////////////////////////////
void sizeBubbleSort(unsigned int *sizeary, char *str[], int height, int weight, int isRflag)
{
    // Allocate memory for temporary string variable
    char *temp_str = (char *)malloc(sizeof(char) * weight);
    for (int i = 0; i < height; i++)
    {
        int temp_size = 0;
        for (int j = 0; j < height - i - 1; j++)
        {
            if (isRflag == 0 && (sizeary[j] < sizeary[j + 1]))
            {
                // Swap file names and sizes
                strcpy(temp_str, str[j]);
                strcpy(str[j], str[j + 1]);
                strcpy(str[j + 1], temp_str);
                temp_size = sizeary[j];
                sizeary[j] = sizeary[j + 1];
                sizeary[j + 1] = temp_size;
            }
            else if (isRflag == 1 && (sizeary[j] > sizeary[j + 1]))
            {
                // Swap file names and sizes
                strcpy(temp_str, str[j + 1]);
                strcpy(str[j + 1], str[j]);
                strcpy(str[j], temp_str);
                temp_size = sizeary[j + 1];
                sizeary[j + 1] = sizeary[j];
                sizeary[j] = temp_size;
            }
            else if (isRflag == 1 && (sizeary[j] == sizeary[j + 1]))
            {
                if (str[j][0] != '.' && str[j + 1][0] == '.')
                {
                    char *com_arr = extractNonHidden(str[j + 1]);
                    if ((strcasecmp(str[j], com_arr) < 0)) // Compare strings case-insensitive
                    {
                        strcpy(temp_str, str[j]); // Swap strings
                        strcpy(str[j], str[j + 1]);
                        strcpy(str[j + 1], temp_str);
                        temp_size = sizeary[j];
                        sizeary[j] = sizeary[j + 1];
                        sizeary[j + 1] = temp_size;
                    }
                }
                else if (str[j][0] == '.' && str[j + 1][0] != '.')
                {
                    char *com_arr = extractNonHidden(str[j]);
                    if ((strcasecmp(com_arr, str[j + 1]) < 0)) // Compare strings case-insensitive
                    {
                        strcpy(temp_str, str[j]); // Swap strings
                        strcpy(str[j], str[j + 1]);
                        strcpy(str[j + 1], temp_str);
                        temp_size = sizeary[j];
                        sizeary[j] = sizeary[j + 1];
                        sizeary[j + 1] = temp_size;
                    }
                }
                else
                {
                    if ((strcasecmp(str[j], str[j + 1]) < 0)) // Compare strings case-insensitive
                    {
                        strcpy(temp_str, str[j]); // Swap strings
                        strcpy(str[j], str[j + 1]);
                        strcpy(str[j + 1], temp_str);
                        temp_size = sizeary[j];
                        sizeary[j] = sizeary[j + 1];
                        sizeary[j + 1] = temp_size;
                    }
                }
            }
            else if (isRflag == 0 && (sizeary[j] == sizeary[j + 1]))
            {
                if (str[j][0] != '.' && str[j + 1][0] == '.')
                {
                    char *com_arr = extractNonHidden(str[j + 1]);
                    if ((strcasecmp(str[j], com_arr) > 0)) // Compare strings case-insensitive
                    {
                        strcpy(temp_str, str[j]); // Swap strings
                        strcpy(str[j], str[j + 1]);
                        strcpy(str[j + 1], temp_str);
                        temp_size = sizeary[j];
                        sizeary[j] = sizeary[j + 1];
                        sizeary[j + 1] = temp_size;
                    }
                }
                else if (str[j][0] == '.' && str[j + 1][0] != '.')
                {
                    char *com_arr = extractNonHidden(str[j]);
                    if ((strcasecmp(com_arr, str[j + 1]) > 0)) // Compare strings case-insensitive
                    {
                        strcpy(temp_str, str[j]); // Swap strings
                        strcpy(str[j], str[j + 1]);
                        strcpy(str[j + 1], temp_str);
                        temp_size = sizeary[j];
                        sizeary[j] = sizeary[j + 1];
                        sizeary[j + 1] = temp_size;
                    }
                }
                else
                {
                    if ((strcasecmp(str[j], str[j + 1]) > 0)) // Compare strings case-insensitive
                    {
                        strcpy(temp_str, str[j]); // Swap strings
                        strcpy(str[j], str[j + 1]);
                        strcpy(str[j + 1], temp_str);
                        temp_size = sizeary[j];
                        sizeary[j] = sizeary[j + 1];
                        sizeary[j + 1] = temp_size;
                    }
                }
            }
        }
    }
    free(temp_str);
}