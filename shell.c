/* This is the only file you should update and submit. */

/* Fill in your Name and GNumber in the following two comment fields
 * Name:
 * GNumber:
 */

#include <assert.h>
#include <ctype.h>

#include "logging.h"
#include "shell.h"

// WORKAROUND: some versions of glibc have stdlib.h and sys/wait.h those contradict
// and mutually hide WIFCONTINUED and WCONTINUED macroses, but they are essential
// for checking a process to have been continued
#if !defined(WIFCONTINUED) || !defined(WCONTINUED)
    #ifndef WIFCONTINUED
        #define WIFCONTINUED(status) ((status) == __W_CONTINUED)
    #endif
    #ifndef WCONTINUED
        #define WCONTINUED 8
    #endif
#endif

/* Constants */
// these are paths where a user's command launched will be looked up if it is not a builting command
static const char *shell_path[] = {"./", "/bin/", "/usr/bin/", "/usr/local/bin/", NULL};
// this is a list of builtin commands those will be more prior than any other user commands
static const char *built_ins[]  = {"quit", "help", "kill", "jobs", "fg", "bg", NULL};

/* required function as your staring point;
 * check shell.h for details
 */

// job ID type
typedef size_t job_id_t;

// job structure consists of:
//   * ID
//   * Process ID the job belongs to
//   * Command line (the command itself and its arguments) that the job was running with
typedef struct job_s
{
    job_id_t id;
    pid_t    pid;
    char     *cmd;
} job_t;
// foreground job is just a job, no other properties unlike to a background job
typedef job_t fg_job_t;

// the global foreground job
fg_job_t g_fg_job;

// there are 2 background job statuses: it is either running or stopped
typedef enum BGJobStatus_e
{
    STOPPED,
    RUNNING,
} BGJobStatus_t;

// background job is a job having a status of execution as an extra property
typedef struct bg_job_s
{
    job_t         job; // job itself
    BGJobStatus_t status; // status of the job executing
} bg_job_t;

// structure that describes a node as a main structure of the backgound jobs list
// node is bidirected
typedef struct bg_job_node_s
{
    // a background node itself
    bg_job_t             job;
    // pointers to the previous and next nodes respectively
    struct bg_job_node_s *next, *prev;
} bg_job_node_t;

// backgound nodes bidirected list
typedef struct bg_jobs_lst_s
{
    // pointer to the head and tail of the list respectively
    bg_job_node_t *head, *tail;
    // size of the list
    size_t size;
} bg_jobs_lst_t;
// the global backgound jobs list
// is is going to be sorted in ascending order
bg_jobs_lst_t g_bg_jobs_lst;

// predicate type that is used to compare jobs with an abstract cb_data
typedef int (*job_predicate_t)(job_t const *job, void const *cb_data);

// the function that adds the job with the status into the list of background jobs
static void add_job_to_bg(job_t const *job, BGJobStatus_t status)
{
    assert(job);
    bg_jobs_lst_t *lst = &g_bg_jobs_lst;
    bg_job_node_t *new_node = (bg_job_node_t*)malloc(sizeof(bg_job_node_t));
    assert(new_node);
    new_node->job.job    = *job;
    new_node->job.status = status;
    // it means that list has been empty so far
    if (0 == lst->size)
    {
        if (0 == new_node->job.job.id)
            new_node->job.job.id = 1;
        // the first node comes into the list
        new_node->next = new_node->prev = NULL;
        lst->head = lst->tail = new_node;
    }
    // ID == 0 means a new job
    else if (0 == new_node->job.job.id)
    {
        // if a job is new one it is required to assign a new job ID
        // that must be greater than the current greatest job ID in the list
        // since list is sorted in ascending order, the current greteast job ID
        // is at the tail of the list
        // therefore, we need ID that is 1 greater than ID of the job at the tail
        new_node->next  = NULL;
        new_node->prev  = lst->tail;
        lst->tail->next = new_node;
        lst->tail       = lst->tail->next;
        lst->tail->job.job.id = lst->tail->prev->job.job.id + 1;
        // there could be the situation when there is the foreground job
        // that were in the list and has had the ID already
        // in order to not interfere with this ID and keep IDs unique for all
        // jobs we need to check whether a new ID assigned is not already busy
        // by the foreground job
        // if it is we just increase it by 1
        if (g_fg_job.id == lst->tail->job.job.id)
            ++lst->tail->job.job.id;
    }
    // it means that the job has already had a Job ID allocated
    else
    {
        // here we need to find where we need to insert a new node
        bg_job_node_t *node = lst->head;
        for (; node && new_node->job.job.id < node->job.job.id; node = node->next)
            ;

        // when we need to insert in front of the head
        if (node == lst->head)
        {
            new_node->prev = NULL;
            lst->head->prev = new_node;
            new_node->next = lst->head;
            lst->head = lst->head->prev;
        }
        // when we need to insert after the tail
        else if (!node)
        {
            new_node->next = NULL;
            lst->tail->next = new_node;
            new_node->prev = lst->tail;
            lst->tail = lst->tail->next;
        }
        // when we need to insert in the middle of the list
        else
        {
            new_node->next = node;
            new_node->prev = node->prev;
            node->prev = new_node;
            new_node->prev->next = new_node;
        }
    }
    // increment the size as a new node has been inserted
    ++lst->size;
}

// a job's predicate function that compare a job's ID with cb_data as a job ID
// returns 1 if IDs are equal, 0 otherwise
static int job_id_predicate(job_t const *job, void const *cb_data)
{
    job_id_t const *p_id = (job_id_t const*)(cb_data);
    return job->id == *p_id;
}

// a job's predicate function that compare a job's PID with cb_data as a job PID
// works like a lambda function
// returns 1 if IDs are equal, 0 otherwise
static int job_pid_predicate(job_t const *job, void const *cb_data)
{
    pid_t const *p_pid = (pid_t const*)(cb_data);
    return job->pid == *p_pid;
}

// the function that look for a node meeting the predicate provided
// returns a node if found, NULL otherwise
static bg_job_node_t* find_bg_job_node(job_predicate_t p, void const *cb_data)
{
    bg_job_node_t *node = g_bg_jobs_lst.head;
    for (; node && !p(&node->job.job, cb_data); node = node->next)
        ;
    return node;
}

// finds a background job node in the list of background jobs by a job ID provided
// returns a node if found, NULL otherwise
static bg_job_node_t* find_bg_job_node_by_id(job_id_t id) { return find_bg_job_node(job_id_predicate, &id); }

// finds a background job node in the list of background jobs by a job PID provided
// returns a node if found, NULL otherwise
static bg_job_node_t* find_bg_job_node_by_pid(pid_t pid) { return find_bg_job_node(job_pid_predicate, &pid); }

// removes the node from the list of background jobs and destroys it
static void remove_bg_job_node(bg_job_node_t *node)
{
    assert(node && 0 != g_bg_jobs_lst.size);

    if (node->prev)
        node->prev->next = node->next;

    if (node->next)
        node->next->prev = node->prev;

    if (node == g_bg_jobs_lst.head)
    {
        g_bg_jobs_lst.head = node->next;
        if (g_bg_jobs_lst.head)
            g_bg_jobs_lst.head->prev = NULL;
    }

    if (node == g_bg_jobs_lst.tail)
    {
        g_bg_jobs_lst.tail = g_bg_jobs_lst.tail->prev;
        if (g_bg_jobs_lst.tail)
            g_bg_jobs_lst.tail->next = NULL;
    }

    // size is one less
    --g_bg_jobs_lst.size;

    free(node);
}

// the function that duplicates the string
static char* sdup(char const *str)
{
    char *dup = (char*)malloc(strlen(str) + 1);
    if (dup)
        strcpy(dup, str);
    return dup;
}

// the handler that receives SIGINT and SIGTSTP in the shell
static void sig_int_stop_handler(int sig)
{
    assert(SIGINT == sig || SIGTSTP == sig);
    // print out the information according to what kind of a signal has been received
    SIGINT == sig ? log_ctrl_c() : log_ctrl_z();
    // if there is the foreground job on then send the signal to it
    if (0 != g_fg_job.pid)
        kill(g_fg_job.pid, sig);
}

// the handler that receives only SIGCHLD in the shell
static void sig_child_handler(int sig)
{
    assert(sig == SIGCHLD);
    int wstatus = 0;
    // waits until any child has fired (exited, signalled, stopped, continued, etc.)
    pid_t child_pid = waitpid(-1, &wstatus, WUNTRACED | WCONTINUED);
    assert(child_pid >= 0);

    // if a child has exited
    if (WIFEXITED(wstatus))
    {
        // if the foreground job has exited just notify about that and clear the foreground job
        if (child_pid == g_fg_job.pid)
        {
            log_job_fg_term(g_fg_job.pid, g_fg_job.cmd);
            free(g_fg_job.cmd);
            memset(&g_fg_job, 0, sizeof(g_fg_job));
        }
        // if the background job has terminated find the job by PID in the list of background jobs,
        // notify about finishing of the job and remove the node from the list
        else
        {
            bg_job_node_t *node = find_bg_job_node_by_pid(child_pid);
            if (node)
            {
                log_job_bg_term(node->job.job.pid, node->job.job.cmd);
                free(node->job.job.cmd);
                remove_bg_job_node(node);
            }
        }
    }
    // if a child has been signalled with a termination
    else if (WIFSIGNALED(wstatus))
    {
        // if the foreground job has been terminated just notify about that and clear the foreground job
        if (child_pid == g_fg_job.pid)
        {
            log_job_fg_term_sig(g_fg_job.pid, g_fg_job.cmd);
            free(g_fg_job.cmd);
            memset(&g_fg_job, 0, sizeof(g_fg_job));
        }
        // if the background has been terminated then find it by PID in the list,
        // notify about what a background job has terminated
        // and remove the node from the list of background jobs
        else
        {
            bg_job_node_t *node = find_bg_job_node_by_pid(child_pid);
            if (node)
            {
                log_job_bg_term_sig(node->job.job.pid, node->job.job.cmd);
                free(node->job.job.cmd);
                remove_bg_job_node(node);
            }
        }
    }
    // if a child has been stopped
    else if (WIFSTOPPED(wstatus))
    {
        // if the foreground process has been stopped then we need to dispose
        // it in the list of background jobs and mark it as a stopped job
        // after these operations there is no the foreground job any longer
        if (child_pid == g_fg_job.pid)
        {
            add_job_to_bg(&g_fg_job, STOPPED);
            log_job_fg_stopped(g_fg_job.pid, g_fg_job.cmd);
            memset(&g_fg_job, 0, sizeof(g_fg_job));
        }
        else
        {
            // find a background job by PID and store the status 'STOPPED' as it has been stopped
            bg_job_node_t *node = find_bg_job_node_by_pid(child_pid);
            if (node)
            {
                node->job.status = STOPPED;
                log_job_bg_stopped(node->job.job.pid, node->job.job.cmd);
            }
        }
    }
    // if a child has been continued
    else if (WIFCONTINUED(wstatus))
    {
        if (child_pid == g_fg_job.pid)
        {
            log_job_fg_cont(g_fg_job.pid, g_fg_job.cmd);
        }
        else
        {
            // find a background job by PID and store the status 'RUNNING' as it has been continued
            bg_job_node_t *node = find_bg_job_node_by_pid(child_pid);
            if (node)
            {
                node->job.status = RUNNING;
                log_job_bg_cont(node->job.job.pid, node->job.job.cmd);
            }
        }
    }
    else
    {
        exit(EXIT_FAILURE);
    }
}

// the function waits until the foreground job has been finished
// of it will be placed into background jobs with clearing its PID
// in the foreground job structure
// clearing PID in the g_fg_job by setting it 0 would mean that
// there is no a foreground job any longer and we can continue execution
// of the shell
static void wait_fg()
{
    while (0 != g_fg_job.pid)
        pause();
}

// this function handles a builtin command with its arguments
static void handle_builtin(char *cmd, char *argv[])
{
    // handle the command 'quit'
    if (0 == strcmp("quit", cmd))
    {
        log_quit();
        exit(EXIT_SUCCESS);
    }
    // handle the command 'help'
    else if (0 == strcmp("help", cmd))
    {
        log_help();
    }
    // handle the command 'kill'
    else if (0 == strcmp("kill", cmd))
    {
        assert(argv[0] && argv[1]);
        // 'kill' builtin command receives two arguments, parse them
        int const sig = atoi(argv[0]);
        int const pid = atoi(argv[1]);
        log_kill(sig, pid);
        kill(pid, sig);
    }
    // handle the command 'jobs'
    else if (0 == strcmp("jobs", cmd))
    {
        // print all the information about background jobs
        log_job_number(g_bg_jobs_lst.size);
        for (bg_job_node_t *node = g_bg_jobs_lst.head; node; node = node->next)
            log_job_details(node->job.job.id,
                            node->job.job.pid,
                            node->job.status == RUNNING ? "Running" : "Stopped",
                            node->job.job.cmd);
    }
    // handle the command 'fg'
    else if (0 == strcmp("fg", cmd))
    {
        assert(argv[0]);
        job_id_t const job_id = atoi(argv[0]);
        // find a background node by job ID
        bg_job_node_t *node = find_bg_job_node_by_id(job_id);
        // if it has been found make it be the foreground and remove from the list of background jobs
        if (node)
        {
            g_fg_job = node->job.job;
            BGJobStatus_t const status = node->job.status;
            remove_bg_job_node(node);
            log_job_fg(g_fg_job.pid, g_fg_job.cmd);
            // if the job has been stoped, resume it since it has become the foreground
            switch (status)
            {
                case STOPPED:
                    kill(g_fg_job.pid, SIGCONT);
                    break;
                case RUNNING:
                    break;
                default:
                    exit(EXIT_FAILURE);
            }
            // wait until the job finishes or the signal has been received
            // that potentionally could change a foreground job properties
            wait_fg();
        }
        // if a background job with the job ID wasn't found then print the error
        else
        {
            log_jobid_error(job_id);
        }
    }
    else if (0 == strcmp("bg", cmd))
    {
        assert(argv[0]);
        job_id_t const job_id = atoi(argv[0]);
        // find a background node by job ID
        bg_job_node_t *node = find_bg_job_node_by_id(job_id);
        // if it has been found make it run if it hasn't been
        if (node)
        {
            log_job_bg(node->job.job.pid, node->job.job.cmd);
            // if the job has been stoped, resume it
            switch (node->job.status)
            {
                case STOPPED:
                    kill(node->job.job.pid, SIGCONT);
                    break;
                case RUNNING:
                    break;
                default:
                    exit(EXIT_FAILURE);
            }
        }
        // if a background job with the job ID wasn't found then print the error
        else
        {
            log_jobid_error(job_id);
        }
    }
    else
    {
        fprintf(stderr, "unknown builtin command '%s'\n", cmd);
        exit(EXIT_FAILURE);
    }
}

/* main */
/* The entry of your shell program */
int main(void)
{
    char cmdline[MAXLINE]; /* Command line */

    /* Initial Prompt and Welcome */
    log_prompt();
    log_help();

    struct sigaction sact;
    memset(&sact, 0, sizeof(sact));
    sact.sa_handler = sig_int_stop_handler;
    // ctrl-C and ctrl-Z are caught in the 'sig_int_stop_handler'
    assert(sigaction(SIGINT, &sact, NULL) == 0);
    assert(sigaction(SIGTSTP, &sact, NULL) == 0);
    memset(&sact, 0, sizeof(sact));
    // sig_child_handler is responsible for catching changing the state of children
    sact.sa_handler = sig_child_handler;
    assert(sigaction(SIGCHLD, &sact, NULL) == 0);

    /* Shell looping here to accept user command and execute */
    while (1)
    {
        char *argv[MAXARGS]; /* Argument list */
        Cmd_aux aux; /* Auxilliary cmd info: check shell.h */
        memset(&aux, 0, sizeof(aux));

        /* Print prompt */
        log_prompt();

        /* Read a line */
        // note: fgets will keep the ending '\n'
        errno = 0;
        fgets(cmdline, MAXLINE, stdin);

        // feof will catch 'ctrl-D' if there is any
        if (feof(stdin))
        {
            log_quit();
            exit(EXIT_SUCCESS);
        }

        // interruptions while reading are skipped
        if (errno == EINTR)
            continue;

        /* Parse command line */
        cmdline[strlen(cmdline) - 1] = '\0';  // remove trailing '\n'
        // duplicate the cmdline received from the shell to afterwards store it in a job
        char *cmd = sdup(cmdline);
        // parse a command line received from the shell
        parse(cmdline, argv, &aux);

        // if the command is empty skip it, run from the start
        if (!argv[0] || '\0' == argv[0][0])
            continue;

        // if the path is starting from a letter then check it for being a builtin command
        if (isalpha(argv[0][0]))
        {
            // check for the command to be builtin
            char const **builtin = built_ins;
            for (; *builtin && 0 != strcmp(*builtin, argv[0]); ++builtin)
                ;

            // if the command is builtin, run it
            if (NULL != *builtin)
            {
                handle_builtin(argv[0], &argv[1]);
                continue;
            }
        }

        // this is done to synchronize the changing
        // in the global list of the background tasks and the foreground job
        // we block signals until all the lists and global identifiers updated
        sigset_t x;
        sigemptyset(&x);
        sigaddset(&x, SIGCHLD);
        sigaddset(&x, SIGINT);
        sigaddset(&x, SIGTSTP);
        sigprocmask(SIG_BLOCK, &x, NULL);

        // fork the process to split it
        //  * one will become a child
        //  * the second will remain parent that called 'fork'
        pid_t pid = fork();
        // this is a child where we should run a new program
        // process ID equal to 0 signals that this is the child born
        if (0 == pid)
        {
            // before calling a new program we release signals for the new child process
            sigprocmask(SIG_UNBLOCK, &x, NULL);

            assert(setpgid(0, 0) == 0);
            if (aux.in_file)
            {
                int const in_f_fd = open(aux.in_file, O_RDONLY);
                assert(in_f_fd > 0);
                assert(dup2(in_f_fd, STDIN_FILENO) >= 0);
            }

            if (aux.out_file)
            {
                int const flags = O_CREAT | O_WRONLY | (aux.is_append ? O_APPEND : O_TRUNC);
                int const out_f_fd = open(aux.out_file, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                assert(out_f_fd > 0);
                assert(dup2(out_f_fd, STDOUT_FILENO) >= 0);
            }

            // if the path is relative
            if ('/' != argv[0][0])
            {
                for (char const **path = shell_path; *path; ++path)
                {
                    size_t const path_len = strlen(*path);
                    size_t const argv0_len = strlen(argv[0]);
                    size_t const size = path_len + argv0_len + 1;
                    char *pathname = malloc(size);
                    strncpy(pathname, *path, path_len);
                    strncpy(&pathname[path_len], argv[0], argv0_len);
                    pathname[size - 1] = '\0';
                    execv(pathname, argv);
                    free(pathname);
                }
            }
            // if the path is absolute
            else
            {
                execv(argv[0], argv);
            }
            log_command_error(argv[0]);
            exit(EXIT_FAILURE);
        }
        // this is the parent, the shell itself
        // process ID not equal to 0 signals that this is the parent
        // that called 'fork' and spawned the child
        else if (pid > 0)
        {
            // storing a new job properties
            job_t job;
            // job ID here is 0 to show that the job is a new one,
            // it matters only if the job is launched as background
            job.id  = 0;
            // store the process ID the job belongs to
            job.pid = pid;
            // store the command line that the job was launched with
            job.cmd = cmd;
            // if the command has been launched in background mode,
            // store it in the list of background jobs and continue execution
            // of the shell
            if (aux.is_bg)
            {
                log_start_bg(job.pid, job.cmd);
                add_job_to_bg(&job, RUNNING);
            }
            // if the job is foreground, save it as the only one foreground job
            // and wait until the signal is raised or foreground job has been finished
            else
            {
                log_start_fg(job.pid, job.cmd);
                g_fg_job = job;
            }

            // upon updating all the global structures we are releasing signals blocked for the shell process
            sigprocmask(SIG_UNBLOCK, &x, NULL);

            // if the current task is foreground, block the shell until the foreground task has finished
            // or a signal releasing the foreground process has occurred
            if (!aux.is_bg)
                wait_fg();
        }
        // negative values of PID signal about terrible problems while forking
        // exit from the shell with failure
        else
        {
            exit(EXIT_FAILURE);
        }
    }
}
/* end main */

// gets the first arg by means of strtok providing a new line
// returns the first non-null token separated by whitespace, or NULL if no token can be fetched
static char* start_arg(char *line) { return strtok(line, " "); }

// gets next arg by means of strtok
// returns the next non-null token separated by whitespace, or NULL if no token can be fetched
static char* get_arg() { return strtok(NULL, " "); }

// parse command line provided by a user and received from the shell
// returns
//   * arguments in a shape of the array of strings where argv[0] and the rest are arguments to argv[0]
//   * aux structure that contains special cases when file IO redirection is used, or background process is run
void parse(char *cmd_line, char *argv[], Cmd_aux *aux)
{
    // starting processing argument by argument those are separated by whitespaces
    for (char *arg = start_arg(cmd_line); arg; arg = get_arg())
    {
        // empty arguments just skip
        if (0 == strlen(arg))
            continue;

        // if there is some redirection used mark it up in the 'aux' structure
        if (0 == strcmp("<", arg) || 0 == strcmp(">", arg) || 0 == strcmp(">>", arg))
        {
            if (arg[0] == '<')
            {
                aux->in_file = get_arg();
            }
            else
            {
                aux->out_file = get_arg();
                aux->is_append = 0 == strcmp(">>", arg);
            }
        }
        // if there is a background used mark it up in the 'aux' structure
        else if (0 == strcmp("&", arg))
        {
            aux->is_bg = 1;
        }
        else
        {
            // store the argument in the argv array
            *argv++ = arg;
        }
    }
    // the last argument must point to NULL to terminate the list of arguments read
    argv[0] = NULL;
}
