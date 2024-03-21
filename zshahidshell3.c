/******************************************************************************

Zainab Shahid
OPSYS Jochen
Extended Shell

*******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <utmp.h>
#include <utmpx.h>

#define MAX_STRING 2058
#define DELIMITER " "
#define MAX_TOKENS 100
#define MAX_TOKEN_LENGTH 100

extern char **environ; //using global variable environ that contains all env variables
int lastBGpid = -1; //global variable to store pid of last background function
pthread_t watchuser_tid; //initialize pthread
int watchuser_thread_created = 0; //initialize variable to track if thread craeated
    
//define struct to store info on background processes
typedef struct BGProcess
{
    pid_t pid;
    struct BGProcess *next; //point to the next node
} BGProcess;

BGProcess *BGProcesses = NULL; //head of linked list, initialize to null

//define struct to store info on redirection handling 
typedef struct Redirection
{
    int input_redirect;
    int output_redirect;
    int append_redirect;
    int error_redirect;
    int append_error_redirect;
    char *input_file;
    char *output_file;
} Redirection;

//define struct for watched users
typedef struct WatchedUser 
{
    char username[UT_NAMESIZE]; //using built in UT_NAMESIZE for max string of username
    struct WatchedUser *next;
} WatchedUser;

//create linked list to store watched users, initialize at null
WatchedUser *watchedUsers = NULL;
pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER; //initialize mutex lock

//function to tokenize input and store the tokens into an array
char **Tokenize(char *input)
{
  //allocate dynamic memory for the array using malloc
  char **tokens = malloc (MAX_TOKENS * sizeof (char *));
  if (tokens == NULL)
    {
      perror ("no input found.");
      exit (EXIT_FAILURE);
    }

  int token_count = 0;		//initialize token_count at 0

  //removing any leading spaces that are not a delimiter
  while (isspace (*input))
    {
      input++;
    }

  //using strtok to tokenize input
  char *token = strtok (input, DELIMITER);
  while (token != NULL && token_count < MAX_TOKENS)
    {
      //create buffer array to allocate memory
      tokens[token_count] = malloc ((strlen (token) + 1) * sizeof (char));

      //using built in command strcpy to copy tokens into buffer array
      strcpy (tokens[token_count], token);

      //increment token count and increment pointer until end of string
      token_count++;

      while (isspace (*++input))
	{
	  //no body needed
	}

      if (token_count >= MAX_TOKENS)
	{			//handle if tokens are more than max
	  break;
	}
	
      //get next token
      token = strtok (NULL, DELIMITER);
    }
  //set array to null before loop terminates
  tokens[token_count] = NULL;
  
  //return values in array
  return tokens;
}

//function to handle redirection process
void handleRedirection(int *fd, int redirect, char *file, int std_fd, int flags)
{
    if (redirect) //check if redirection is required
    {
        //open file with specified flags anf permissions
        *fd = open(file, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        if (*fd < 0) //if the file descriptor is negative, an error occurred while openeing the file
        {
            perror("Error opening file"); //print the error message 
            exit(EXIT_FAILURE); //exit with a failure status
        }
        
        close(std_fd); //close the stanard file descripter (stdin, stdout, stderr)
        
        //duplicate file descripter using dup() and check if its equal to the standard file descripter
        if (dup(*fd) != std_fd)
        {
            perror("dup"); //error message to check
            exit(EXIT_FAILURE);
        }
        close(*fd); //close the file descriptor for the file
    }
}

//function to add a background process to the linked list
int addBGprocess(int pid)
{
    struct BGProcess *new_process = malloc(sizeof(struct BGProcess)); //use malloc to allocate memory for background processes
    if (!new_process) //error checking
    {
        perror("Error allocating memory");
        exit(EXIT_FAILURE);
    }

    new_process->pid = pid; //set pid of new background process
    new_process->next = BGProcesses; //add new process to linked list of bg processes
    BGProcesses = new_process; //head of linked list points to new process

    return pid; //return the PID of adde process
}

//function to handle pipe operators
void handle_pipe(char **tokens, int num_tokens, int pipe_index, Redirection *redirections, int pipe_stderr)
{
    int pipe_fd[2]; //declare array for file descriptors of pipe

    //create pipe and check for errors
    if (pipe(pipe_fd) < 0)
    {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    //create array to store the tokens for left side of pipe
    char **left_tokens = malloc(sizeof(char *) * (pipe_index + 1));
    
    //copy tokens from the original array to the new array
    for (int i = 0; i < pipe_index; i++)
    {
        left_tokens[i] = tokens[i];
    }
    left_tokens[pipe_index] = NULL; //add a NULL terminator to the array

    //create a new array to store the tokens for right side of pipe
    char **right_tokens = malloc(sizeof(char *) * (num_tokens - pipe_index));
    
    //copy tokens from the original array to new array
    for (int i = pipe_index + 1, j = 0; i < num_tokens; i++, j++)
    {
        right_tokens[j] = tokens[i];
    }
    right_tokens[num_tokens - pipe_index - 1] = NULL; //ddd NULL terminator to the right_tokens array

    pid_t left_pid = fork(); //create a new process for the left side

    if (left_pid == 0) //if its the left child process
    {
        close(pipe_fd[0]); //close the read end of the pipe
        dup2(pipe_fd[1], STDOUT_FILENO); //redirect stdout to the write end of the pipe
        
        if (pipe_stderr) //if stderr should be piped
        {
            dup2(pipe_fd[1], STDERR_FILENO); //redirect stderr to the write end of the pipe
        }
        close(pipe_fd[1]); //close the write end

        execvp(left_tokens[0], left_tokens); //execute left command
        perror("exec"); //if the command fails, print error
        exit(EXIT_FAILURE);
    }

    pid_t right_pid = fork(); //create new process for the right side of pipe

    if (right_pid == 0) //if its the right child process
    {
        close(pipe_fd[1]); //close the write end
        dup2(pipe_fd[0], STDIN_FILENO); //redirect stdin to the read end of the pipe
        close(pipe_fd[0]); //close the read end

        execvp(right_tokens[0], right_tokens); // Execute the right command
        perror("exec"); //if command fails, print error
        exit(EXIT_FAILURE);
    }

    close(pipe_fd[0]); //close read end of pipe in parent process
    close(pipe_fd[1]); //close write end of pipe in parent process

    waitpid(left_pid, NULL, 0);    //don't wait for the left child to finish
    waitpid(right_pid, NULL, 0);   //but wait for the right child to finish

    //free tokens allocated for both arrays
    free(left_tokens); 
    free(right_tokens);
}

//function to create thread for watchuser to monitor logins and mutex lock implementation
void *watchuser_thread(void *arg)
{
    while (1)
    {
        sleep(20); //sleep before checking the utmpx file

        pthread_mutex_lock(&users_mutex); //mutex lock to protect linked list
        
        //set file position of upmtx to beginning
        setutxent();
        
        //read entries from upmtx
        struct utmpx *entry;
        while ((entry = getutxent()) != NULL)
        {
            //check if entry is a user process
            if (entry->ut_type == USER_PROCESS)
            {  
                //iterate through linked lsit
                WatchedUser *current = watchedUsers;
                while (current != NULL)
                { 
                    //if user entry matches a watched user
                    if (strcmp(entry->ut_user, current->username) == 0)
                    {
                        //print message to confirm
                        printf("%s has logged on %s from %s\n", entry->ut_user, entry->ut_line, entry->ut_host);
                        break;
                    }
                    //move position to the next user in list
                    current = current->next;
                }
            }
        }
        
        //close the file
        endutxent();
        
        //unlock mutex lock
        pthread_mutex_unlock(&users_mutex);
    }

    return NULL;
}

//function to handle watchuser command
void handle_watchuser(char **tokens, int num_tokens)
{
   //check if number of tokens is less than 2 or more than 3
            if (num_tokens < 2 || num_tokens > 3)
            {
            printf("Error: must enter username \n");
            return;
            }
        
        //lock mutex for the watchedUsers linked list
        pthread_mutex_lock(&users_mutex);
            
            //if there are 2 tokens, add a new user to the watchedUsers list
            if (num_tokens == 2)
            {
                WatchedUser *new_user = (WatchedUser *)malloc(sizeof(WatchedUser)); 
                strncpy(new_user->username, tokens[1], UT_NAMESIZE);
                new_user->next = watchedUsers;
                watchedUsers = new_user;
                printf("Added %s to watch list.\n", new_user->username);
                
            //if the watchuser thread is not running, create it
            if (!watchuser_thread_created)
            {
            pthread_create(&watchuser_tid, NULL, watchuser_thread, NULL);
            watchuser_thread_created = 1;
            }
        }
        //else if last token is 'off', remove user from linked list
            else if (strcmp(tokens[2], "off") == 0)
            {
            WatchedUser **current = &watchedUsers; //set current pointer to address of head
                while (*current != NULL) //while current node is not null
                {
                    if (strcmp((*current)->username, tokens[1]) == 0) //if current username matches inputted username
                    {
                    WatchedUser *tmp = *current; //use temporary pointer to current node
                    *current = (*current)->next; //update current pointer to point to next node in list
                    free(tmp); //free temporary pointer memory
                    printf("Removed %s from watch list.\n", tokens[1]); 
                    break; //break once user has been removed
                    }   
            current = &((*current)->next); //update current pointer to the address of nect node
                }
            }   

        //unlock the mutex
        pthread_mutex_unlock(&users_mutex);

}

//function to print linked list of watched users
void print_watchedusers(int num_tokens)
{
    //check if number of tokens is more than 1
    if (num_tokens > 1)
    { //error checking
        printf("Error: Too many arguments.\n");
        return;
    }
    
    pthread_mutex_lock(&users_mutex); //lock mutex
    
    if (watchedUsers == NULL) //if there are no users in list
    { //print error
        printf("There are currently no users in the list.\n");
    }
    
    //else print users
    else 
    {
    printf("Watched users:\n");
    
    //crate current pointer
    WatchedUser *current = watchedUsers;
    while (current != NULL) //while current is not null, traverses through linked list
    {
        printf("  - %s\n", current->username); //print username of current watched user
        current = current->next; //move current pointer to next user in list
    }
}
    pthread_mutex_unlock(&users_mutex); //unlock mutex after accessing list
}

//function to handle fg command
void fg(char **tokens, int num_tokens)
{
    int pid; //initiaze int for pid

    if (num_tokens < 2) //if no argument after 'fg'
    {
        if (lastBGpid == -1) //check last backgroung process, if none exists, print error
        {
            fprintf(stderr, "Error: No background process to bring to foreground.\n");
            return;
        }
        pid = lastBGpid; //set the pid of that background process to the lastBGpid
    }
    else
    {   //if format of pid is wrong
        if (!(sscanf(tokens[1], "[%d]", &pid) == 1 || sscanf(tokens[1], "%d", &pid) == 1))
        { //error printing
            fprintf(stderr, "Error: Invalid PID format.\n");
            return;
        }
    }
    
    //find the process in the list of background processes
    struct BGProcess *process = BGProcesses;
    while (process != NULL && process->pid != pid)
    {
        process = process->next;
    }
    if (process == NULL)
    {
        fprintf(stderr, "Error: Process %d is not a background process.\n", pid);
        return;
    }

    //send SIGCONT signal to the process to bring it into the foreground and ensure it continues
    kill(pid, SIGCONT);

    //wait for the process to finish
    int status;
    waitpid(pid, &status, 0);

    //remove the process from the list of background processes
    struct BGProcess *prev = NULL; //initialize pointer to previous node
    process = BGProcesses;
    //iterate through linked lis to find node with matching pid
    while (process != NULL && process->pid != pid)
    {
        prev = process; //set previous node to current node
        process = process->next; //set current node to next node
    }
    if (process != NULL) //if current node is not null
    {
        if (prev != NULL) //if its not head of linked list
        {
            prev->next = process->next; //set previous node to point to next node
        }
        else //if removed process is at head of list
        {
            BGProcesses = process->next; //set linked list head to point to next node
        }
        free(process); //free memory of reomved node
    }

    
    printf("Foreground process %d", pid);
}

//function to terminate program using exit commands
void ExitFunc(int terminate)
{
  printf ("exiting shell program.");
  exit (terminate);		//terminate program
}

//function to get the print current working directory
void printCWD()
{
 char cwd[MAX_STRING];
  if (getcwd (cwd, sizeof (cwd)) != NULL)
    {
      printf ("< @ %s > ", cwd);
    }
}

//function to change directory and specify path
void ChangeDirectory(char **tokens, int num_tokens) 
{
  //changing to home directory if no path is specified
  if (tokens[1] == NULL)
    {
      char *homeDIR = getenv ("HOME");	//get home directory using getenv
      int change = chdir (homeDIR);
     //setenv("PWD", homeDIR, 1); // update PWD environment variable
     printf ("changed directory to %s\n", homeDIR);
     // printCWD();
     
    // setenv("CWD", homeDIR, 1);
    }

  //else change to specified directory
  else
    {
      //using built in command stat to check file type to check if 2nd input is a directory
      struct stat path;
      int stat_check = stat (tokens[1], &path);	//check second token

      if (stat_check != 0)
	{			//if the path does not exist
	  printf ("Error: Directory does not exist.\n");
	}
      else if (!S_ISDIR (path.st_mode))
	{			//if path exists but is not a directory
	  printf ("Error: %s is not a directory.\n", tokens[1]);
	}
      else
	{			//else if path exists and is a directory
	  int change = chdir (tokens[1]);	//change directory to specified path
	  if (change < 0)
	    {
	      printf ("not successful.");
	    }
	  else
	    {
	       // setenv("PWD", cwd, 1); // update PWD environment variable
	      printf ("changed directory to %s\n", tokens[1]);
	       printCWD();
	    }
	}
    }	//the cd function only checks the first two tokens and disregards the rest
}

//function to get the current pid
void getPID()
{
  pid_t pid = getpid ();
  printf ("current PID is %d\n", pid);
}

//function to handle the kill command
void kill_command(char **tokens, int num_tokens)
{
  //determine the signal to send
  int signal_num = SIGTERM;	//default signal is SIGTERM
  if (num_tokens == 3) //if 3 inputs
    {
      //get the signal number from the second token
      char *signal_str = tokens[1] + 1;	//disregard the -
      signal_num = atoi (signal_str);	//using atoi to convert char into int
    }

  //extract the pid from the last token
  pid_t pid = atoi (tokens[num_tokens - 1]);

  //check if pid exists
  if (kill (pid, signal_num) == -1) //if kill returns -1
    {
      if (errno == ESRCH) //use error code ESRCH
	{
	  fprintf (stderr, "kill: no such process\n");
	}
      else
	{
	  perror ("kill");
	}
    }
}

//function that uses built in command setenv to set environment variables
void set_env(char **tokens, int num_tokens)
{				//takes string as argumument
  //if no variable input
  if (num_tokens == 1)
    {				//using built in command setenv
      setenv (tokens[0], "", 1);	//sets variable to empty string
    }
  //if one variable input
  else if (num_tokens == 2)
    {
      setenv (tokens[1], tokens[1], 1);	//set variable to specified variable
    }
  //if two variable input
  else if (num_tokens == 3)
    {
      setenv (tokens[2], tokens[1], 1);	//sets variable to value of first
    }

  //if > two tokens 
  else if (num_tokens > 2)
    {
      fprintf (stderr, "Error: too many arguments.\n");	//print error message
    }

}

//function to print the environment variables
void print_env()
{
  char **env = environ;
  while (*env != NULL)
    {
      printf ("%s\n", *env);	//print each element until null
      env++;
    }
}

//function to handle which command
void which(char **tokens, int num_tokens)
{
  if (num_tokens < 2)
    {				//if no file path is specified
      printf ("Enter file name\n");	//prompt user for file name
      return;
    }
  
  //handle -a command as second argument
  bool all_locations = false;
  if (strcmp(tokens[1], "-a") == 0)
    {
      if (num_tokens < 3) 
        {
          printf ("Enter file name\n");
          return;
        }
      all_locations = true; //set flag for all locations
    }
  
  char **path_tokens = Tokenize (getenv ("PATH"));	//tokenize the path variable
    bool found = false;

   //loop through each directory in the path variable
  for (int i = 0; path_tokens[i] != NULL; i++)
    {
      printf("Checking directory: %s\n", path_tokens[i]); //printing iteration through path tokens for debug purposes
      //create string that contains path to the executable file
      char file_path[MAX_TOKENS];
      snprintf (file_path, MAX_TOKENS, "%s/%s", path_tokens[i], tokens[1]);

      //check if the file is executable
    if (access (file_path, F_OK) == 0 && access (file_path, X_OK) == 0)
	{			
	  printf ("%s\n", file_path);	
	  found = true;
	  break;
	}
    }

    //if command is not found, print error
  if (!found) {
    printf("file not found\n");
  }
  

  //count the path tokens
  int path_token_count = 0;
  while (path_tokens[path_token_count] != NULL)
    {
      path_token_count++;
    }

  //free the memory allocated for the path tokens
  for (int i = 0; i < path_token_count; i++)
    {
      free (path_tokens[i]);
    }
  free (path_tokens);
}

//function to list directories
void list(char **tokens, int num_tokens, int is_background)
{
    //check if the command should be run in the background
    if (is_background) //if background flag was set
    {
        pid_t pid = fork(); //create a new child process
        if (pid < 0) //error checking
        {
            fprintf(stderr, "Error: Failed to fork process.\n");
            exit(EXIT_FAILURE);
        }
        else if (pid == 0) //child process
        {
            setpgid(0, 0); //create new process group for the background process
        }
        else //parent process prints the pid of background process to user
        {
            printf("Background process started: [%d]\n", pid);
            return; //return to prompting user for input
        }
    }

//execution of list command
  DIR *dir;
  struct dirent *ent;
//using built in commands opendir and readdir by including dirrent.h
  if (num_tokens == 1)
    {
      //if no arguments list files in cwd
      dir = opendir (".");	//open current directory
      if (dir == NULL)
	{
	  perror ("opendir");	//error message if it fails
	  return;
	}
      while ((ent = readdir (dir)) != NULL)
	{			//read directory once its opened
	  printf ("%s\n", ent->d_name);	//prints files in directory
	}

      closedir (dir);		//close the directory
    }
  else
    {				//if more than one token
      //list files in each specified directory
      for (int i = 1; i < num_tokens; i++)
	{
	  printf ("\n%s:\n", tokens[i]);
	  dir = opendir (tokens[i]);	//open each directory
	  if (dir == NULL)
	    {
	      perror ("no directory found");
	      continue;
	    }

	  while ((ent = readdir (dir)) != NULL)
	    {			//read each directory
	      printf ("%s\n", ent->d_name);
	    }
	  closedir (dir);	//close directory
	}
    }
}

//function to check if command is redirection, a built-in, relative or absolute path and handle accordingly
bool checkCommand(char *first_token, char **tokens, int num_tokens, int is_background, Redirection *redirections)
{
    //initialize redirections
    redirections->input_redirect = 0;
    redirections->output_redirect = 0;
    redirections->append_redirect = 0;
    redirections->input_file = NULL;
    redirections->output_file = NULL;

    for (int i = 0; i < num_tokens; ++i)
    {
        if (strcmp(tokens[i], "<") == 0) //check for input redirection operator
        {
            redirections->input_redirect = 1; //set redirection input flag to 1
            if (i + 1 < num_tokens)
            {
                redirections->input_file = tokens[i + 1]; //store input file name
                tokens[i] = NULL; //remove the "<" token
                tokens[i + 1] = NULL; //remove input file token
            }
        }
        else if (strcmp(tokens[i], ">") == 0) //check for output redirection
        {
            redirections->output_redirect = 1; //set output flag to 1
            if (i + 1 < num_tokens)
            {
                redirections->output_file = tokens[i + 1]; //store output file name
                tokens[i] = NULL; //remove the ">" token
                tokens[i + 1] = NULL; //remove output file token
            }
        }
        else if (strcmp(tokens[i], ">>") == 0) //check for append redirection
        {
            redirections->append_redirect = 1; //set append flag to 1
            if (i + 1 < num_tokens)
            {
                redirections->output_file = tokens[i + 1]; //store output file
                tokens[i] = NULL; //remove the ">>" token
                tokens[i + 1] = NULL; //remove output file token
            }
        }
    }
//built in command handling
    if (strcmp(first_token, "cd") == 0)
    {
        ChangeDirectory(tokens, num_tokens);
        return false;
    }
    else if (strcmp(first_token, "setenv") == 0)
    {
        set_env(tokens, num_tokens);
        return false;
    }
    else if (strcmp(first_token, "pid") == 0)
    {
        getPID();
        return false;
    }
    else if (strcmp(first_token, "pwd") == 0)
    {
        printCWD();
        return false;
    }
    else if (strcmp(first_token, "kill") == 0)
    {
        kill_command(tokens, num_tokens);
        return false;
    }
    else if (strcmp(first_token, "which") == 0)
    {
        which(tokens, num_tokens);
        return false;
    }
    else if (strcmp(first_token, "printenv") == 0)
    {
        print_env();
        return false;
    }
    else if (strcmp(first_token, "fg") == 0)
    {
        fg(tokens, num_tokens);
        return false;
    }
    else if (strcmp(first_token, "watchuser") == 0)
    {
    handle_watchuser(tokens, num_tokens);
    return false;
    }
    else if (strcmp(tokens[0], "printusers") == 0)
    {
    print_watchedusers(num_tokens);
    return false;
    }
    else if (strcmp(first_token, "list") == 0 || strcmp(first_token, "ls") == 0)
    {
        list(tokens, num_tokens, is_background);
        return false;
    } 
    //check if command is an absolute or relative path
    else if (first_token[0] == '/' || strncmp(first_token, "./", 2) == 0
             || strncmp(first_token, "../", 3) == 0)
    {
        if (access(first_token, X_OK) == 0) //using access to check if file is executable
        { 	
           return true; //return true if it is
        }
        else
        {
            fprintf(stderr, "Error: External command is not executable or not found.\n");
        }
    }
    else
    {
        printf("<%s>: ", first_token);
        printf("Command not found.\n"); //handle invalid commands
    }
    return false;
}

//function to handle external commands
void externalCommand(char *first_token, char **tokens, int num_tokens, int is_background, Redirection *redirections)
{
    pid_t pid = fork();

    if (pid == 0) //child process
    {
        if (is_background) { //if background flag is set
            setpgid(0, 0); //create new process group for background process
        }
        
        //declare variables for file descriptors
        int fd_in, fd_out, fd_err;

        //handle input redirection
        handleRedirection(&fd_in, redirections->input_redirect, redirections->input_file, STDIN_FILENO, O_RDONLY);

        
        int flags = O_WRONLY | O_CREAT; //set flags for output
        if (redirections->append_redirect) flags |= O_APPEND;
        else flags |= O_TRUNC;
        //handle output redirection
        handleRedirection(&fd_out, redirections->output_redirect || redirections->append_redirect, redirections->output_file, STDOUT_FILENO, flags);

        //set flags for error
        if (redirections->append_error_redirect) flags |= O_APPEND;
        else flags |= O_TRUNC;
        //handle error redirection
        handleRedirection(&fd_err, redirections->error_redirect || redirections->append_error_redirect, redirections->output_file, STDERR_FILENO, flags);
        
        //execute the command in the child process
        execvp(first_token, tokens);
        // if execvp returns, it must have failed, so print an error message
        fprintf(stderr, "Error executing command '%s'\n", first_token);
        exit(EXIT_FAILURE);
    }
    else if (pid < 0) //check to see if fork failed
    {
        perror("Error forking");
        exit(EXIT_FAILURE);
    }
    else //parent process
    {
        if (is_background) //if the background flag is set
    {
        printf("Background process started: [%d]\n", pid); //print the pid
        lastBGpid = addBGprocess(pid); //store the last background pid
    }
    else //wait for the child process to finish if it's a foreground process
    {
        int status;
        waitpid(pid, &status, 0);
    }
    }
}

//main function  
int main()
{
  char input[MAX_STRING];
  int is_background = 0; //initalizing flag for background process
   
   //initialize redirection with default values
    Redirection redirections = {0, 0, 0, 0, 0, NULL, NULL};
    
  
    //loop to keep prompting user for input and not exit after each command
  while (1)
    {
        //reap any zombie background processes before printing the prompt
      while (waitpid(-1, NULL, WNOHANG) > 0)
      {
          //no body
      }
      
      printCWD (); //print prompt
      fflush (stdout);		//i noticed prompt was delayed so im flushing the output stream
      fgets (input, MAX_STRING, stdin); //get user input
      input[strcspn (input, "\n")] = '\0';	//using strcspn to remove newline character from input

      char **tokens = Tokenize (input); //tokenize the input
      int num_tokens = 0; //initiaze number of tokens as 0
      
      //loop through each value in array and count the number of tokens
      while (tokens[num_tokens] != NULL)
	    {
        num_tokens++;
        }
	
   //check if input is empty
    if (tokens[0] == 0)
	{
	  continue;		//and continue prompting user for input
	}
	
      //check for exit function before executing other commands that may require fork
    else if (strcmp (tokens[0], "quit") == 0 || strcmp (tokens[0], "exit") == 0
	  || strcmp (tokens[0], "logout") == 0)
	{
	  ExitFunc (0);
	}
	
	//execute all other commands
	else
	{
	//check if last token is an ampersand
    if (strcmp(tokens[num_tokens-1], "&") == 0) 
    {
    is_background = 1; //set the background flag
    tokens[num_tokens - 1] = NULL; //setting last token '&' to null
    num_tokens--; //deincrementing the number of tokens
    }
	
     if (checkCommand(tokens[0], tokens, num_tokens, is_background, &redirections)) 
        {  //if checkCommand returns true, call executeCommand
            externalCommand(tokens[0], tokens, num_tokens, is_background, &redirections);
        }
        
	for (int i = 0; i < num_tokens; i++)
    {
        if (strcmp(tokens[i], "|") == 0)
        {
            //check for pipe operator, and call handle_pipe()
            handle_pipe(tokens, num_tokens, i, &redirections, 0);  //stderr set to 0
            break;
        }
        else if (strcmp(tokens[i], "|&") == 0)
        {
            //check for pipe& operator,and call handle_pipe()
            handle_pipe(tokens, num_tokens, i, &redirections, 1); //stderr set to 1
            break;
        }
    }
            
    //free memory for background processes linked list
    BGProcess *current = BGProcesses, *tmp; //create pointers current and temporary
    while (current != NULL) //while current is not null (traverse until the end of list)
    {
    tmp = current; //set temporary pointer to current node
    current = current->next; //update current node to next node
    free(tmp); //free memory of node pointed at by tmp
    }
	
      for (int i = 0; i < num_tokens; i++) //free memory for char array
	{
	  free (tokens[i]);
	}
      free (tokens); //free memory for char pointers
    
    }	
    }
    //if thread was created
        if (watchuser_thread_created)
        {
        pthread_cancel(watchuser_tid); //cancel the thread using its thread id
        pthread_join(watchuser_tid, NULL); //wait for thread to cancel and join w null(realease thread resources)
        }
        
  return 0;

}

