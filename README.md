# Shell-V2
Extended Version of Original Shell
Zainab Shahid

Important info about changes and additions to my functions/implementations and my methodology:
This Shell is an extended version of my Shell V1.

Note, my shell does not recognize sleep but will recognize /bin/sleep 30 &

watchuser_thread() function:
I read up on pthreads a lot in forums. I used the void *arg parameter as a generic pointer to pass any type to the function when its created using pthread_create. Apparently this is a common practice in C. After lots and lots of forums and research, I'm not even sure I implemented it right. Originally, I define my own MAX_STRING for the maximum size of the username, however forums suggested using 'ut_namesize' since it was already defined by the uptmx.h header file and would be less buggy.

handle_watchuser() function:
I originally had all this implementation on main but then decided to use it because calling it from main was too buggy. I called it from the checkcommand() function like i did all other functions.

print watched users() function:
I added this to check if my watchusers linked list was working properly. Enter the command 'printusers' and it should work.

handleRedirections() function:
This function accepts the file descriptor, redirection flag, file name, standard file descriptor (stdin, stdout, stderr), and flags for opening the file. If redirection is required, it opens the specified file with the appropriate flags and permissions. It then closes the standard file descriptor, duplicates the new file descriptor, and closes the original file descriptor.

The handle_pipe() function:
Accepts the command tokens, the number of tokens, the pipe index, an array of Redirection structures, and a flag indicating whether stderr should be piped. The function creates a pipe and devides the tokens into left and right sides of the pipe. It creates two child processes, one for the left side and another for the right side. In the left child process, it redirects stdout (and stderr) to the write end of the pipe and executes the left-side command. In the right child process, it redirects stdin to the read end of the pipe and executes the right-side command. The parent process waits for the right child process to finish before continuing.

fg_command() function:
After researching about the built in command 'fg', I wanted my shell to handle all implementations of it, such as the job specification number followed by the mod symbol. But I stuck to just using the pid, either in brackets or by itself. Initally, the implementation was really simple but it wasn't even checking if the pid entered was a valid pid and a valid background process, so I implemented it further by using kill and the sigcont (research told me that it makes sure that the process continues running after beign brought into the foreground). Also, since my shell wasn't accepting 'sleep' as a command, i couldnt run a background process long enough to implement fg and see if it would actually bring a background process into the foreground.

bool checkCommand() function:
In my previous shell program, I was checking for any built in commands in the main function, which was making it unnessarily long. I created a new function to check if the command is a built in or external command. Previously, I had messed up the check for an absolute or relative path and the fork() implementation. The function returns false if the command is a built in and calls the proper function. The function then checks any non-built in commands to see if they are a relative or absolute path. If they are, it uses access() to check if the command is executable, then it returns true. At first I had this command call the externalCommand function directly and then return true, but then the point of having the function be of type bool was pointless, so I decided to add implementation in main() to check if the function is returning true and call the externalCommand function.

externalCommand() function:
In my previous shell, the implementation for creating a child process was also in main, and once I realized that I had to build on that to implement pipes, I decided to create a new function for processing external commands. I also know its good practice to keep the main function as short as possible. This function forks a child process regardless, but runs a child process in the background if the background flag is set.

I tried to implement this function in many ways. first as a void, then an int, then a bool, then finally a pid_t. I only chose to implement it as a pid_t because I was attempting to print a statement that returned the pid to let me know that a process was running in the background for my own testing purposes. Once I realized that in order to implement the 'fg' command, I'd have to implement a linked list to store backgorund process info and keep track of them, I changed the function to be of type void, and implemented a different function to handle background processes.

which() function:
I was having an issue where I was getting a 'double free()' error, but it wasn't interfering with the functionality. However, I only freed all tokens once in main(). Except for in the which() function where I freed the path_tokens, but that was apart of a separate array. After many attempts at debugging and headaches, I found that my error was because the loop in my which function to free the path tokens was set to run for "MAX_TOKENS" amount of times. So I added implementation to count the number of path tokens, and only free the memory that was allocated for the path tokens so that there was no issue with a double free.

Previously, I also didn't implement the -a argument, so I added that as well.
It was working perfectly but towars the end it started showing the path but returning 'file not found' for everything (so will fix this in the future).

addBGprocess():
This accepts a pid as its argument. It allocates memory for a new background process, sets the pid of the new background process, and adds it to the linked list of background processes. The function returns the process ID of the added process. However, i have no idea whether the processes were actually running in the background or it was just printing a message that it was. even when adding the ampersand after 'ls' it was still outputting the ls output, which i wasnt sure if it was supposed to do?

list() function:
I thought a lot about which of the built in commands to fork as a child process. I ended up messing heavily with a lot of my existing code because I got caught up in trying to make most built-ins a child proccess that were then not communicating with the parent and frankly unneccesary. Only list() made sense, so I added implementation to check if '&' was at the end of the command line and fork a child process for list. I also was running into an issue where 'ls -l' wasn't working but if i defined the path, as '/bin/ls -l' it was. I tried using echo $PATH to check what was up but my shell wasn't recognizing those environment variables. I started implementing code to force it to regognize the '$', 'echo' and others but I decided to fix this later since there were other more important things to do.

main():
I compeletely reworked this function. Firstly, main checks for the exit command. Then, it checks for an '&' and sets the background flag if its at the end of the command line. Then I set the last token to null and deincremented the number of tokens so that it didn't mess with further implementation.
Then, it checks commands by calling the checkCommand function, which handles built-in commands and returns true if the comamnd is not built in. Then, it calls the externalCommand function to handle external commands. Its also used to free all memory allocated and cancel pthreads from the watchuser implementation.
