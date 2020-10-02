#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>

#define INITIAL_CAP 10

void promptUser(char** input);
void exitCmd(pid_t* currentBPIDs, int numBPIDs);
void cdCmd(int argsc, char* args[512]);
void statusCmd(int exitMethod);
void addBPID(int** currentBPIDs, int pid, int* numBPIDs, int* capacity);
void removeBPID(int** currentBPIDs, int pid, int* numBPIDs);
int parseArgs(char* input, char* args[512], int* argsc);
void execCommand(int cmdOption, pid_t* currentBPIDs, int* numBPIDs, int* capacity, int argsc, char* args[512], int* exitMethod);
int checkBG(char* args[512], int argsc);
int checkInputRedir(char* args[512], int argsc);
int checkOutputRedir(char* args[512], int argsc);
void parseCommandArgs(char* parsedCommandArgs[512], char* args[512], int argsc);
bool needsParsing(char* args[512], int argsc);
void checkFinishedBPIDs(int** currentBPIDs, int* numBPIDs);
void redirInput(char* args[512], int inputRedirIndex, int BGIndex);
void redirOutput(char* args[512], int outputRedirIndex, int BGIndex);
void catchSIGTSTP(int signo);

// Global variable used for signal handling.
struct sigaction SIGTSTP_action = {0}, ignore_action = {0}, default_action = {0};
sigset_t signal_set;
bool backgroundToggle = true;
bool fgProcessRunning = false;

int main(){

   // Creates a dynamic array for holding the currently running background proceesses pids.
   int capacity = INITIAL_CAP;
   int* currentBPIDs = malloc(capacity*sizeof(int));
   int numBPIDs = 0;

   // Creates an array to store the command line arguments and how many there are.
   int argsc;
   char* args[512];
   char* input;
   // This integer will be used to determine whether or not a built-in command
   // was given by the user.
   int cmdOption;

   // Integer used to determine how a child process exits.
   int exitMethod = -5;
  
   // Clears the global signal set decalred above, and add SIGTSTP to it.
   sigemptyset(&signal_set);
   sigaddset(&signal_set, SIGTSTP);

   // Sets up ignore and default sig handling for two sigaction structs.
   ignore_action.sa_handler = SIG_IGN;
   default_action.sa_handler = SIG_DFL;
   // Sets up a signal handler function for SIGTSTP.
   SIGTSTP_action.sa_handler = catchSIGTSTP;
   sigfillset(&SIGTSTP_action.sa_mask);
   SIGTSTP_action.sa_flags = SA_RESTART;

   // Parent process ignores SIGINT.
   sigaction(SIGINT, &ignore_action, NULL);

   // Parent process catches and handles SIGTSTP.
   sigaction(SIGTSTP, &SIGTSTP_action, NULL);

   // The main shell loop.
   while(1){
      do{
	 // Checks to see if any background processes have completed before prompting the user.
	 checkFinishedBPIDs(&currentBPIDs, &numBPIDs);
	 // Prompt the user and store their input.
	 promptUser(&input);
	 // Parse users input into arguments. Returns 0, 1, or 3 to cmdOption depending on whether the user
	 // wants to execute a built-in command. If 3, then the command is not built-in.
	 cmdOption = parseArgs(input, args, &argsc);
      // Repeat this process if the first character of te line is #. 
      }while(args[0][0] == '#');
      // Execute the given command.
      execCommand(cmdOption, currentBPIDs, &numBPIDs, &capacity, argsc, args, &exitMethod);
   }


   free(currentBPIDs);
   return 0;
}

// This function provides the interface for the prompt.
void promptUser(char** input){
   int result;
   size_t inputSize = 0;
   size_t numChars;
   do{
      clearerr(stdin);
      numChars = -5;
      printf(": ");
      fflush(stdout);
      numChars = getline(input, &inputSize, stdin);
   // Reprompts the user if they hit enter.   
   }while(strcmp(*input, "\n") == 0);
   // Delete the trailing newline.
   (*input)[strcspn((*input), "\n")] = '\0';
}

// This is the built-in exit command.
void exitCmd(pid_t* currentBPIDs, int numBPIDs){
   int i;
   // If there are any currently running background processes,
   // loop through them and send a SIGTERM to each one.
   for(i = 0; i < numBPIDs; i++){
      kill(currentBPIDs[i], SIGTERM);
   }
   // Parent proccess exits.
   exit(0);
}

// The build-in cd command.
void cdCmd(int argsc, char* args[512]){
   int result;
   // Variable for storing the current working directory.
   char currDir[100];
   // If there is only one arguemnt (the cd command by itself), the change
   // the cwd to the HOME environment variable.
   if(argsc == 1){
      result = chdir(getenv("HOME"));
   // Otherwise, change the cwd to whatever the next argument is.   
   }else{
      result = chdir(args[1]);
      if(result != 0){
	 perror("Directory does not exist");
	 fflush(stdout);
      }
   }
}

// The build-in status command.
void statusCmd(int exitMethod){
   int exitStatus, exitSignal;
   // If the process terminates normally, provide the exit status.
   if(WIFEXITED(exitMethod) != 0){
      exitStatus = WEXITSTATUS(exitMethod);
      printf("exit value %d\n", exitStatus);
      fflush(stdout);
   // If the process was terminated by a signal, provide the signal number.
   }else if(WIFSIGNALED(exitMethod) != 0){
      exitSignal = WTERMSIG(exitMethod);
      printf("terminated by signal %d\n", exitSignal);
      fflush(stdout);
   }
}

// Adds a background process pid to the currentBPIDs array.
void addBPID(int** currentBPIDs, int pid, int* numBPIDs, int* capacity){
   if(*numBPIDs > *capacity){
      *currentBPIDs = realloc(*currentBPIDs, sizeof(*currentBPIDs)*2);
      *capacity = sizeof(*currentBPIDs)*2;
   }
   (*currentBPIDs)[(*numBPIDs)] = pid;
   (*numBPIDs)++;
}

// Removes the given pid from the currentBPIDs array.
void removeBPID(int** currentBPIDs, int pid, int* numBPIDs){
   int i, pidIndex;
   int* temp = malloc((*numBPIDs-1)*sizeof(int));
   for(i = 0; i < *numBPIDs; i++){
      if((*currentBPIDs)[i] == pid){
	 pidIndex = i;
	 break;
      }
   }
   memmove(temp, *currentBPIDs, (pidIndex+1)*sizeof(int));
   memmove(temp+pidIndex, (*currentBPIDs)+(pidIndex+1), (*numBPIDs - pidIndex)*sizeof(int));
   free(*currentBPIDs);
   *currentBPIDs = temp;
   (*numBPIDs)--;
}
// Takes the user's input and tokenizes it, deliminating by spaces. Stores each
// token into the args array. It returns a result, based on what command the user
// wants to execute.
int parseArgs(char* input, char* args[512], int* argsc){
   int result; //0 = exit; 1 = cd; 2 = status; 3 = non-build in command.
   char* token;
   char* rest = input;
   int i;
   *argsc = 0;
   memset(args, '\0' , 512);

   while((token = strtok_r(rest, " ", &rest))){
      args[(*argsc)] = token;
      (*argsc)++;
   }

   // Loops through the args array. If any string contains $$, it replaces that
   // with the shell's pid.
   for(i = 0; i < *argsc; i ++){
      char* t = strstr(args[i], "$$");
      if(t){
	 char shellPID[6];
	 sprintf(shellPID, "%d", getpid());
	 strcpy(t, shellPID);
      } 
   }

   if(strcmp(args[0], "exit") == 0) result = 0;
   else if(strcmp(args[0], "cd") == 0) result = 1;
   else if(strcmp(args[0], "status") == 0) result = 2;
   else result = 3;
   

   return result;
   
}
// Executes that command that the user provides. Creates a child process if the command is not build in.
void execCommand(int cmdOption, pid_t* currentBPIDs, int* numBPIDs, int* capacity, int argsc, char* args[512], int* exitMethod){
   // These variables will be used to determine if a command specifies < > or &, and what index of args they are in.
   int BGIndex, inputRedirIndex, outputRedirIndex;
   // These arguments will be used if < > & are specified, otherwise args will be used.
   char* parsedCommandArgs[512];
   // Execute the given command.
   if(cmdOption == 0) exitCmd(currentBPIDs, *numBPIDs);
   else if(cmdOption == 1) cdCmd(argsc, args);
   else if (cmdOption == 2) statusCmd(*exitMethod);
   // If the command is not build in, spawn a child process.
   else if( cmdOption == 3){
      pid_t spawnPID = -5;
      // Determine if & is specified. BGIndex will be 0 if not, or it will be whatever index that & was specified in args.
      BGIndex = checkBG(args, argsc);
      spawnPID = fork();
      switch(spawnPID){
	 case -1: { perror("Error creating child process\n"); break; } 
         case 0: {

		    // If the child process is NOT a background process, then SIGINT can terminate it.
		    if(BGIndex == 0) sigaction(SIGINT, &default_action, NULL);
		    else args[BGIndex] = "\0";
		    // A child process ignores SIGTSTP.
		    sigaction(SIGTSTP, &ignore_action, NULL);
		    // If < is specified, get its index. Otherwise inputRedirIndex will be 0.
		    inputRedirIndex = checkInputRedir(args, argsc);
		    // If > is specified, get its index. Otherwise, outputRedirIndex will be 0.
	      	    outputRedirIndex = checkOutputRedir(args, argsc);
		    // Redirect input
		    redirInput(args, inputRedirIndex, BGIndex);
		    if(inputRedirIndex != 0){ 
		       args[inputRedirIndex] = "\0";
		       args[inputRedirIndex + 1] = "\0";
		    }
		    // Redirect output.
		    redirOutput(args, outputRedirIndex, BGIndex);
	      	    if(outputRedirIndex != 0){
		       args[outputRedirIndex] = "\0";
		       args[outputRedirIndex + 1] = "\0";
		    }
		    int i;
		    for(i = 0; i < argsc; i++){
		       printf("%s\n", args[i]);
		    }
		    // Further parse the arguments, seperating the command arguments from < > and &.
		    execvp(args[0], args);
		    // Kill the child if execution fails.
		    perror("Error executing command: ");
		    fflush(stdout);
		    exit(1);
		    break;
         }
	 default: {
		     // If & is specified, and background processes are allowd, then add this child's
		     // pid to the currentBPIDs array and don't wait on it.
		     if(checkBG(args, argsc) && backgroundToggle == true){
			addBPID(&currentBPIDs, spawnPID, numBPIDs, capacity);
                        printf("background pid is %d\n", spawnPID);
			fflush(stdout);
		     }
		     // Otherwise, this is a foreground process.
		     else{
			// This global bool indicates that a fg process is currently running.
			fgProcessRunning = true;
			// Block SIGTSTP, while a fg process is currently running.
			sigprocmask(SIG_BLOCK, &signal_set, NULL);
			// wait on the child.
			pid_t childPID = waitpid(spawnPID, exitMethod, 0);
			// If terminated by a signal, get the child's status.
			if(WIFSIGNALED(*exitMethod) != 0) statusCmd(*exitMethod);
			// Unblock SIGTSTP.
			sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
			fgProcessRunning = false;
		     }
		     break;
		  }
      }
   }
}

// Checks if & is specified
// returns its index if true, or 0 otherwise.
int checkBG(char* args[512], int argsc){
   int index = 0, i;
   for(i = 1; i < argsc; i++){
      if(strcmp(args[i], "&") == 0){
	 index = i;
	 break;
      }
   }
   return index;
}

// Checks if < is specified
// returns its indec if true, or 0 otherwise.
int checkInputRedir(char* args[512], int argsc){
   int index = 0, i;
   for(i = 1; i < argsc; i++){
      if(strcmp(args[i], "<") == 0){
	 index = i;
	 break;
      }
   }
   return index;
}

// Checks if > is specified.
// returns its index if true, or 0 otherwise.
int checkOutputRedir(char* args[512], int argsc){
   int index = 0, i;
   for(i = 1; i < argsc; i++){
      if(strcmp(args[i], ">") == 0){
	 index = i;
	 break;
      }
   }
   return index;
}

// If < > or & are specified, create a copy of args, so that < > or & can be dealt as non command args.
void parseCommandArgs(char* parsedCommandArgs[512], char* args[512], int argsc){
   int i;
   int index = 0;
   bool needsParsing = false;
   for(i = 1; i < argsc; i++){
      if(strcmp(args[i], "&") == 0 || strcmp(args[i], "<") == 0 || strcmp(args[i], ">") == 0){
	 index = i;
	 needsParsing = true;
	 break;
      }
   }
   memset(parsedCommandArgs, '\0', 512);
   memcpy(parsedCommandArgs, args, index*sizeof(args));
}

// Redundant function used to see if < > or & are specified.
bool needsParsing(char* args[512], int argsc){
   int i;
   bool needsParsing = false;
   for(i = 1; i < argsc; i++){
      if(strcmp(args[i], "&") == 0 || strcmp(args[i], "<") == 0 || strcmp(args[i], ">") == 0){
	 needsParsing = true;
	 break;
      }
   }
   return needsParsing;
}

// Loops through the currentBPIDs array and checks of any of those processes have finished.
void checkFinishedBPIDs(int** currentBPIDs, int* numBPIDs){
   pid_t childPID = -5;
   int exitMethod;   
   int i = 0;
   while(i < (*numBPIDs)){
      childPID = waitpid((*currentBPIDs)[i], &exitMethod, WNOHANG);
      if(childPID != 0 && childPID != -1){
	 // If a process is done, print out its pid and status. 
	 printf("background pid %d is done: ", childPID);
	 fflush(stdout);
	 statusCmd(exitMethod);
	 removeBPID(currentBPIDs, childPID, numBPIDs);
      }else{
	 i++;
      }
   }
}

// Redirects input from  the specified file.
void redirInput(char* args[512], int inputRedirIndex, int BGIndex){
   int sourceFD, result;
   if(inputRedirIndex != 0 ){
      // if & is not specified, redirect stdin
      if(BGIndex == 0){
	 sourceFD = open(args[inputRedirIndex + 1], O_RDONLY);
	 if(sourceFD == -1) { perror("source open()"); exit(1); }
	 result = dup2(sourceFD, 0);
      }else{
	 // if & is specified and a file is not specified from the user, redirect stdin from /dev/null
         if(strcmp(args[inputRedirIndex + 1], "&") == 0){
	    sourceFD = open("/dev/null", O_RDONLY);
	    result = dup2(sourceFD, 0);
	 }else{
	    sourceFD = open(args[inputRedirIndex + 1], O_RDONLY);
	    if(sourceFD == -1) { perror("source open()"); exit(1); }
	    result = dup2(sourceFD, 0);
	 }
      }
   }
}

// Redirects stdout to the given file.
void redirOutput(char* args[512], int outputRedirIndex, int BGIndex){
   int targetFD, result;
   if(outputRedirIndex != 0){
      // if & is not specified redirect stdout as normal.
      if(BGIndex == 0){
	 targetFD = open(args[outputRedirIndex + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	 if(targetFD == -1) { perror("target open()"); exit(1); }
	 result = dup2(targetFD, 1);
      // If & is specified, and a file is not provided by the user, redirect stdout to /dev/null
      }else{
	 if(strcmp(args[outputRedirIndex + 1], "&") == 0){
	    targetFD = open("/dev/null", O_WRONLY);
	    result = dup2(targetFD, 1);
	 }else{
	    targetFD = open(args[outputRedirIndex + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	    if(targetFD == -1) { perror("target open()"); exit(1); }
	    result = dup2(targetFD, 1);
	 }
      }
   }
   // If a bakground process redirects stdin, but not stdout, send the ouput to /dev/null
   else if(outputRedirIndex == 0 && BGIndex != 0){
      targetFD;
      targetFD = open("/dev/null", O_WRONLY);
      result = dup2(targetFD, 1);
   }		       
}

// The signal handling funciton for SIGTSTP.
void catchSIGTSTP(int signo){
   // Everytime this function is executed, toggle whether or not bg processes are allowed.
   backgroundToggle = !backgroundToggle;
   // If a fg proccess is currently running, then don't include a : at the end of the message.
   if(backgroundToggle == false){
      if(fgProcessRunning == true){
	 char* message = ("\nEntering foreground-only mode (& is now ignored)\n");
	 write(STDOUT_FILENO, message, 50);
	 fflush(stdout);
      }else{
	 char* message = ("\nEntering foreground-only mode (& is now ignored)\n: ");
	 write(STDOUT_FILENO, message, 52);
	 fflush(stdout);
      }
   }else if(backgroundToggle == true){
      if(fgProcessRunning == true){
	 char* message = ("\nExiting foreground-only mode\n");
	 write(STDOUT_FILENO, message, 30);
	 fflush(stdout);
      }else{
	 char* message = ("\nExiting foreground-only mode\n: ");
	 write(STDOUT_FILENO, message, 32);
	 fflush(stdout);
      }
   }
}
