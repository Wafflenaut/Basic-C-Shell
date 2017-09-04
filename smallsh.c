#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

typedef int bool;
#define true 1
#define false 0
#define smallBuffer 256
#define mediumBuffer 512
#define maxChildren 500
#define maxLineLength 2049 // 2048 plus \0
#define maxArgs 513 // command and 512 arguments
//global variable for ctrl-z and ctrl-c signal handlers
bool bgAllowed = true;
bool sigInterrupted = false;
bool printForegroundOnly = false;
bool printBackgroundOK = false;



//Catches SIGTSTP signal (e.g. Ctrl-Z) to halt/allow background
//processing
void catchSIGTSTP(int signo){
    //char* message;

    //if background processing is blocked, allow it
    if(bgAllowed == false){
        bgAllowed = true;
        printBackgroundOK = true;
        //message = "\nExiting foreground-only mode\n";
        //write(STDOUT_FILENO, message, 30);
    }
    //else if background processing is allowed, block it
    else{
        bgAllowed = false;
        printForegroundOnly = true;
        //message = "\nEntering foreground-only mode (& is now ignored)\n";
        //write(STDOUT_FILENO, message, 50);
    }
}

//expands instances of "$$" within a string
void expandDollarz(char* source, char* pidNumber){
    char originalString[mediumBuffer];
    char tempString[mediumBuffer];
    char* startOfExpand = NULL;
    char* afterExpand = NULL;



        memset(originalString, '\0', sizeof(originalString));
        memset(tempString, '\0', sizeof(tempString));
        strcpy(originalString, source);

        //if the original string contains a "$$" substring
        if(strstr(originalString, "$$") != NULL){
            //set a pointer to the beginning of the "$$" substring
            //then overwrite it with the pidnumber
            strcpy(tempString, originalString);
            startOfExpand = strstr(tempString, "$$");
            strcpy(startOfExpand, pidNumber);

            //set a pointer to the beginning of the substring of the original string
            //then add the size of the substring - the '\0' to get the location of the
            //second portion of any string to expand strings where "$$" appears in the middle
            afterExpand = strstr(originalString, "$$");
            afterExpand += (sizeof("$$") - sizeof(char));
            strcat(tempString, afterExpand);

            //copy the expanded string back to the original string passed in
            strcpy(source, tempString);
        }
}

//exits the shell after terminating all active background processes
void exitShell(pid_t* childProcesses){
    int i;

    //iterate through all the child processes and terminate any that are active
    for(i = 0; i < maxChildren; i++){
        if(childProcesses[i] > 0){
            kill(childProcesses[i], SIGTERM);
        }
    }

    //exit shell successfully
    exit(0);
}

//prints the exit/termination signal of the last child process terminated
void statusShell(int response){

    //checks for exit and terminated signal and displays to user
    //since only one can be true no need for nested statements
    if(WIFEXITED(response)){
        printf("exit value %d\n", WEXITSTATUS(response));
        fflush(stdout);
    }
    if(WIFSIGNALED(response)){
        printf("terminated by signal %d\n", WTERMSIG(response));
        fflush(stdout);
    }

}


//changes directory to an absolute or relative path
//if argument passed in is null, goes to the home directory
void cdShell(char* newDir){

    //if the new directory is null, direct to the home directory
    if(newDir == NULL){

        //if changedir cannot occur, print error
        if(chdir(getenv("HOME")) == -1){
            perror("Failed changing to HOME\n");
            fflush(stdout);
        }

    }
        //if the argument is not null, change to that directory
    else{
        //if changedir unsuccesfull, print error
        if(chdir(newDir) == -1){
            perror("Failed changing directory\n");
            fflush(stdout);
        }

    }

}

void smallShell() {
    size_t bufferSize = 0;
    int responseStatus = -5;
    bool leaveShell = false, inBackground = false, validateError = false;
    bool outputToFile = false, inputFromFile = false;
    char readBuffer[maxLineLength];
    char outputFile[maxLineLength];
    char pidNumber[mediumBuffer];
    int outputFileStatus;
    char inputFile[maxLineLength];
    int inputFileStatus;
    char *arguments[maxArgs];
    char *readString = NULL;
    char *token;
    bool pidAdded = false;
    int i = 0, argCounter = 0, numChars = 0;
    pid_t pid;
    pid_t childProcesses[maxChildren];
    char argBuffer[maxArgs][smallBuffer];

    //set all initial child processes to -1
    for(i = 0; i < maxChildren; i++){
        childProcesses[i] = -1;
    }


    //create string for shell pid
    sprintf(pidNumber, "%d", (int)getpid());

    //do this until user exits
    //though the value checked is never altered except
    //during testing - requires use of exit command in shell
    do {

        //clear the buffers and reset various variables
        memset(readBuffer, '\0', sizeof(readBuffer));
        memset(outputFile, '\0', sizeof(outputFile));
        memset(inputFile, '\0', sizeof(inputFile));
        readString = NULL;

        for (i = 0; i < maxArgs; i++) {
            memset(argBuffer[i], '\0', sizeof(argBuffer[i]));
        }

        for (i = 0; i < maxArgs; i++) {
            arguments[i] = NULL;
        }
        argCounter = 0;
        inBackground = false;
        validateError = false;
        inputFromFile = false;
        outputToFile = false;

        if(printBackgroundOK == true){
            printf("\nExiting foreground-only mode\n");
            fflush(stdout);
            printBackgroundOK = false;
            //write(STDOUT_FILENO, message, 50);
        }
        else if(printForegroundOnly == true){
            printf("\nEntering foreground-only mode (& is now ignored)\n");
            fflush(stdout);
            printForegroundOnly = false;
            //write(STDOUT_FILENO, message, 30);
        }
        else {
            //print the prompt and read input from the user
            printf(": ");
            fflush(stdout);
            numChars = getline(&readString, &bufferSize, stdin);

            //clear error if getline error
            if (numChars == -1) {
                clearerr(stdin);
                //printf("\n");
                //fflush(stdout);
            }

            //tokenize the string prior to the newline character
            token = strtok(readString, "\n");

            //only copies if token is not null (string wasn't empty)
            //strcopying a null value causes a segmentation fault
            if (token != NULL) {
                strcpy(readBuffer, token);
            }

            //if the string did not start with a # or is an empty string (token was null) it should be run
            //(not a comment or empty line)
            //otherwise ignore input
            if (strncmp(readBuffer, "#", 1) != 0 && token != NULL) {

                //tokenizes the first string the user entered (the command)
                token = strtok(readBuffer, " ");

                //copies the command to the argBuffer if it wasn't NULL
                if (token != NULL) {
                    strcpy(argBuffer[argCounter], token);
                    argCounter++;
                }
                //then begins reading every subsequent argument and adding to argBuffer
                while (token != NULL) {
                    token = strtok(NULL, " ");
                    if (token != NULL) {
                        strcpy(argBuffer[argCounter], token);
                        argCounter++;
                    }
                }

                //checks to see if the commands match the three created commands
                //and performs those functions if so
                if (strcmp(argBuffer[0], "exit") == 0) {
                    //exits the shell and kills all child processes
                    exitShell(childProcesses);
                } else if (strcmp(argBuffer[0], "status") == 0) {
                    //checks the status of the last child to terminate/exit
                    statusShell(responseStatus);
                } else if (strcmp(argBuffer[0], "cd") == 0) {
                    if (argCounter >= 2) {
                        //expands any path and then
                        //changes to the path given if more than one argument
                        expandDollarz(argBuffer[1], pidNumber);
                        cdShell(argBuffer[1]);
                    } else {
                        //cd to home
                        cdShell(NULL);

                    }
                    //otherwise the shell will attempt to execute a non-shell command
                } else {
                    //do additional processing on the arguments to remove extra commands and perform validation
                    //if more than one argument, verify inputs (if only one, it can be rejected by exec failure
                    if (argCounter >= 2) {
                        //if the last character is & (run in background)
                        if (strcmp(argBuffer[argCounter - 1], "&") == 0) {
                            //clear the last, set run in background to true, decrement argcounter
                            memset(argBuffer[argCounter - 1], '\0', sizeof(argBuffer[argCounter - 1]));
                            inBackground = true;
                            argCounter--;
                        }

                        //run twice, since there may be an input and an output
                        for (i = 0; i < 2; i++) {
                            //check if the last argument is a > or < (an error)
                            if (strcmp(argBuffer[argCounter - 1], ">") == 0 ||
                                strcmp(argBuffer[argCounter - 1], "<") == 0) {
                                validateError = true;
                            }
                                //if argCounter - 2 is greater than or equal to 1, there is room for an input/output file
                            else {
                                if (argCounter - 2 >= 1) {
                                    if (strcmp(argBuffer[argCounter - 2], ">") == 0) {
                                        //already an output file, two outputfile requests, error
                                        if (outputToFile == true) {
                                            validateError = true;
                                        } else {
                                            //a valid output file request has been found
                                            //set bool to true, copy the argument to the outputfilename
                                            //and clear those arguments and decrement argument counter
                                            outputToFile = true;
                                            strcpy(outputFile, argBuffer[argCounter - 1]);
                                            memset(argBuffer[argCounter - 1], '\0', sizeof(argBuffer[argCounter - 1]));
                                            memset(argBuffer[argCounter - 2], '\0', sizeof(argBuffer[argCounter - 2]));
                                            argCounter -= 2;
                                        }
                                    } else if (strcmp(argBuffer[argCounter - 2], "<") == 0) {
                                        if (inputFromFile == true) {
                                            validateError = true;
                                        } else {
                                            //a valid input file request has been found
                                            //set bool to true, copy the argument to the inputfilename
                                            //and clear those arguments and decrement argument counter
                                            inputFromFile = true;
                                            strcpy(inputFile, argBuffer[argCounter - 1]);
                                            memset(argBuffer[argCounter - 1], '\0', sizeof(argBuffer[argCounter - 1]));
                                            memset(argBuffer[argCounter - 2], '\0', sizeof(argBuffer[argCounter - 2]));
                                            argCounter -= 2;

                                        }
                                    }
                                }
                            }
                        }

                    }

                    if (validateError == true) {
                        for (i = 0; i < argCounter; i++) {
                            printf("Argument %d: %s\n", i + 1, argBuffer[i]);
                            fflush(stdout);
                        }

                        printf("Error in input\n");
                        fflush(stdout);
                    } else {

                        pid = fork();
                        //if process is child
                        if (pid == 0) {

                            //restores default sigint handling for child processes if foreground
                            if(inBackground != true) {
                                struct sigaction childSignals = {0};
                                childSignals.sa_handler = SIG_DFL;
                                sigaction(SIGINT, &childSignals, NULL);
                            }

                            //if input is to come from a file
                            if (inputFromFile == true) {

                                //attempt to open input file and redirect input
                                inputFileStatus = open(inputFile, O_RDONLY);
                                //if open failed, print error
                                if (inputFileStatus == -1) {
                                    printf("cannot open %s for input\n", inputFile);
                                    fflush(stdout);
                                    exit(1);
                                }
                                dup2(inputFileStatus, STDIN_FILENO);

                                close(inputFileStatus);

                            }
                                //else, if input not from a file and running in the background
                            else if (inBackground == true) {
                                //attempt to redirect input to dev null
                                inputFileStatus = open("/dev/null", O_RDONLY);
                                if (inputFileStatus == -1) {
                                    printf("cannot open dev/null for input\n");
                                    fflush(stdout);
                                    exit(1);
                                }
                                dup2(inputFileStatus, STDIN_FILENO);
                                close(inputFileStatus);
                            }
                            //if output is to go to a file
                            if (outputToFile == true) {

                                //attempt to open output file and redirect output
                                outputFileStatus = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
                                if (outputFileStatus == -1) {
                                    printf("cannot open %s for output\n", outputFile);
                                    fflush(stdout);
                                    exit(1);
                                }
                                dup2(outputFileStatus, STDOUT_FILENO);

                                close(outputFileStatus);
                            }
                                //else if output not from a file and running in background, redirect to dev/nul
                            else if (inBackground == true) {
                                //attempt to redirect output to dev null here
                                outputFileStatus = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
                                //if error opening dev/null
                                if (outputFileStatus == -1) {
                                    printf("cannot open dev/null for output\n");
                                    fflush(stdout);
                                    exit(1);
                                }
                                dup2(outputFileStatus, STDOUT_FILENO);

                                close(outputFileStatus);
                            }

                            //set up an array of char points to satisfy input of execvp
                            for (i = 0; i < argCounter; i++) {
                                expandDollarz(argBuffer[i], pidNumber);

                                arguments[i] = argBuffer[i];
                            }

                            //set the element after last argument to NULL for execvp and execute it
                            arguments[argCounter] = NULL;
                            execvp(arguments[0], arguments);
                            fprintf(stderr, "%s: ", arguments[0]);
                            perror("");

                            exit(1);

                            //error attempting to fork
                        } else if (pid == -1) {
                            perror("Fork Failed");

                        }
                            //else process is the parent
                        else {

                            //if argument list had a "&" at the end and background processing is allowed
                            if (inBackground == true && bgAllowed == true) {

                                //set the first empty spot in the array to the child process being run in the background
                                pidAdded = false;
                                i = 0;

                                //check for an empty spot in the array and add the latest pid to it
                                while (i < maxChildren && pidAdded != true) {
                                    if (childProcesses[i] == -1) {
                                        childProcesses[i] = pid;
                                        pidAdded = true;

                                    }
                                    i++;
                                }

                                printf("background pid is %d\n", pid);
                                fflush(stdout);
                            } else {
                                //since running in foreground - wait for child to finish
                                waitpid(pid, &responseStatus, 0);

                                //if the foreground process was interrupted, print the status after cancelled
                                //and reset bool

                                if(WIFSIGNALED(responseStatus)){
                                    printf("terminated by signal %d\n", WTERMSIG(responseStatus));
                                    fflush(stdout);
                                }
                            }
                        }


                    }

                }

            }

            //print any child processes that have terminated
            for (i = 0; i < maxChildren; i++) {
                //if child process is greated than 0 (valid)
                if (childProcesses[i] > 0) {
                    //then if process has terminated
                    if (waitpid(childProcesses[i], &responseStatus, WNOHANG) != 0) {
                        //print message with pid and the response status
                        printf("background pid %d is done: ", childProcesses[i]);
                        fflush(stdout);
                        statusShell(responseStatus);
                        //set the printed child process to -1 (invalid/empty)
                        childProcesses[i] = -1;

                    }
                }
            }
        }

    }while (leaveShell != true);

    }



int main() {

    //set up the signal handlers
   // struct sigaction shellSIG = {0};
    struct sigaction shellSTP = {0};

    struct sigaction shellSIG = {0};
    shellSIG.sa_handler = SIG_IGN;
    sigfillset(&shellSIG.sa_mask);
    shellSIG.sa_flags = 0;
    sigaction(SIGINT, &shellSIG, NULL);

    shellSTP.sa_handler = catchSIGTSTP;
    sigfillset(&shellSTP.sa_mask);
    shellSTP.sa_flags = 0;
    sigaction(SIGTSTP, &shellSTP, NULL);

    //run the shell
    smallShell();
    return 0;
}