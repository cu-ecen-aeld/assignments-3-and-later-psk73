#include "systemcalls.h"
#include <stdlib.h> //system
#include <unistd.h> //execv
#include <sys/wait.h> //wait
#include <stdio.h>
#include <errno.h>
#include <string.h>

//below for open..
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

  printf("%s: Input is: %s", __FUNCTION__, cmd);
  /*
   * PSK-DONE  add your code here
   *  Call the system() function with the command set in the cmd
   *   and return a boolean true if the system() call completed with success
   *   or false() if it returned a failure
   */
  if (0 == system(cmd)) {
    printf("%s: returning true", __FUNCTION__);
    return true;
  } else {
    printf("%s: returning false", __FUNCTION__);
    return false;
  }
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
  va_list args;
  va_start(args, count);
  char *command[count + 1];
  int i;
  for (i = 0; i < count; i++) {
    command[i] = va_arg(args, char *);
  }
  va_end(args);

  fflush(stderr);

  command[count] = NULL;
  // this line is to avoid a compile warning before your implementation is complete
  // and may be removed
  command[count] = command[count];

  /*
   * PSK-DONE:
   *   Execute a system command by calling fork, execv(),
   *   and wait instead of system (see LSP page 161).
   *   Use the command[0] as the full path to the command to execute
   *   (first argument to execv), and use the remaining arguments
   *   as second argument to the execv() command.
   *
   */
  int status;
  int pid;

  pid = fork();
  if (pid == -1)
  {
    printf("%s: ERROR Fork failed\n",__FUNCTION__);
    exit(-1);
  }

  if(pid == 0)
  {
    //Child
    //Execute the command using execv
    execv(command[0],&command[1]);
    printf("%s: ERROR execv failed\n",__FUNCTION__);
    exit(-1);
  }

   //Parent
   printf("%s: (cmd %s) Parent: child pid is %d\n",__FUNCTION__,command[0],pid);
   //Wait for the child to be done
   if (waitpid(pid, &status, 0) == -1)
   {
      //Error waiting on child
      printf("%s: ERROR waiting on child \n",__FUNCTION__);
      return false;
   }
   if(status == -1)
   {
      //Abnormal child termination
      printf("%s: ERROR Abnormal child termination status\n",__FUNCTION__);
      return false;
   }

   printf("%s: (cmd %s) Parent: returning true\n",__FUNCTION__,command[0]);
   return true;
}


/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
  va_list args;
  va_start(args, count);
  char *command[count + 1];
  int i;
  for (i = 0; i < count; i++) {
    command[i] = va_arg(args, char *);
  }
  va_end(args);
  fflush(stderr);

  command[count] = NULL;
  // this line is to avoid a compile warning before your implementation is complete
  // and may be removed
  // command[count] = command[count];

  /*
   * PSK-DONE
   *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
   *   redirect standard out to a file specified by outputfile.
   *   The rest of the behaviour is same as do_exec()
   *
   */
  int status;
  int pid;
  //Open file for redirection
  int fd = open(outputfile,O_WRONLY|O_TRUNC|O_CREAT,0666);
  if (fd < 0)
  {
      printf("%s: Error opening redirection\n",__FUNCTION__);
      exit(-1);
  }

  pid = fork();

  if (pid == -1)
  {
    printf("%s: ERROR Fork failed\n",__FUNCTION__);
    exit(-1);
  }

  if(pid == 0)
  {
    //Child
    //Copy stdout to fd
    if(dup2(fd,1)<0)
    {
        printf("%s: Error in child with dup2\n",__FUNCTION__);
        exit(-1);
    }
    //Copy stderr to fd
    if(dup2(fd,2)<0)
    {
        printf("%s: Error in child with dup2\n",__FUNCTION__);
        exit(-1);
    }
    close(fd);
    //Execute the command using execv
    execv(command[0],&command[1]);
    printf("%s: ERROR execv failed\n",__FUNCTION__);
    exit(-1);
  }

  //Parent
  printf("%s: (cmd %s) Parent: child pid is %d\n",__FUNCTION__,command[0],pid);
  close(fd);
  //Wait for the child to be done
  if (waitpid(pid, &status, 0) == -1)
  {
    //Error waiting on child
    printf("%s: ERROR waiting on child \n",__FUNCTION__);
    return false;
  }
  if(status == -1)
  {
    //Abnormal child termination
    printf("%s: ERROR Abnormal child termination status\n",__FUNCTION__);
    return false;
  }

  printf("%s: (cmd %s) Parent: returning true\n",__FUNCTION__,command[0]);
  return true;
}
