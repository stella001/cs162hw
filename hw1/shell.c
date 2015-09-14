#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "io.h"
#include "parse.h"
#include "process.h"
#include "shell.h"

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_quit(tok_t arg[]);
int cmd_help(tok_t arg[]);
int cmd_pwd(tok_t arg[]);
int cmd_cd(tok_t arg[]);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(tok_t args[]);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_quit, "quit", "quit the command shell"},
  {cmd_pwd, "pwd", "show the current working directory"},
  {cmd_cd, "cd", "change the current directory to the specific one"},
};

/**
 * Prints a helpful description for the given command
 */
int cmd_help(tok_t arg[]) {
  for (int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++) {
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  }
  return 1;
}

/**
 * Quits this shell
 */
int cmd_quit(tok_t arg[]) {
  exit(0);
  return 1;
}

int cmd_pwd(tok_t arg[]) {
  char cwd[1024];
  if (getcwd(cwd, 1024) != NULL) {
    printf("%s\n", cwd);
  }
  return 1;
}

int cmd_cd(tok_t arg[]) {

  chdir(arg[0]);
  return 1;
}

/**
 * Looks up the built-in command, if it exists.
 */
int lookup(char cmd[]) {
  for (int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++) {
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0)) return i;
  }
  return -1;
}

/**
 * Check the file exisit and executable
 */

bool isExecutable(char *path) {
  struct stat stat_buf;
  return (stat(path, &stat_buf) == 0 && stat_buf.st_mode & S_IXUSR);
}

char* concat(char *s1, char *s2);
char* copychar(char* s);
/**
 * Get the environment tokens combined with
 * the 1st input in the command line.
 * retrun the pointer to the tokens
 **/

tok_t *get_multiplePath(char *filePath) {
  char *name = "PATH";
  char *val;
  val = getenv(name);
  // parse the env
  tok_t *tokens = get_toks(val);
  // need to manufact the tokens to add the tok_t's val
  // since we have more tokens and the re-paste the contents
  // a new space is alloc
  tok_t *res = malloc(MAXTOKS * sizeof(tok_t));
  // for the 1st one just copy the tok_t to the new results
  res[0] = (tok_t)copychar(filePath); 
  tok_t *iter = tokens;
  int i = 1;
  while ((*iter) != NULL) {
    res[i] = (tok_t)concat(tokens[i - 1], filePath);
    i++;
    iter++;
  }
  return res;
}

void freeMulPath(tok_t *path) {
  tok_t *iter = path;
  while ((*iter) != NULL) {
    free(*iter);
    iter++;
  }
  free(path);
}

char* concat(char *s1, char *s2) {
  char *result = (char*)malloc(strlen(s1) + strlen(s2) + 2);
  //+1 for the zero-terminator
  //in real code you would check for errors in malloc here
  strcpy(result, s1);
  int len = strlen(result);
  result[len] = '/';
  result[len + 1] = 0;
  strcat(result, s2);
  return result;
}

char* copychar(char *s) {
  char *res = (char*)malloc(strlen(s) + 1);
  strcpy(res, s);
  return res;
}


/**
 * Intialization procedures for this shell
 */
void init_shell() {
  /* Check if we are running interactively */
  shell_terminal = STDIN_FILENO;

  // isatty() test whether a STDIN_FILENO is an open terminal
  shell_is_interactive = isatty(shell_terminal);

  if(shell_is_interactive){
    /* Force the shell into foreground */
    while(tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int shell(int argc, char *argv[]) {
  char *input_bytes;
  tok_t *tokens;
  int line_num = 0;
  int fundex = -1;
  int status;
  init_shell();
  if (shell_is_interactive)
    /* Please only print shell prompts when standard input is not a tty */
    fprintf(stdout, "%d: ", line_num);

  while ((input_bytes = freadln(stdin))) {
    tokens = get_toks(input_bytes);
    fundex = lookup(tokens[0]);
    if (fundex >= 0) {
      cmd_table[fundex].fun(&tokens[1]);
    } else {
      /* REPLACE this to run commands as programs. */
      pid_t cpid = fork();
      if (cpid == 0) {
        // in the child branch
        // read the env variable
        tok_t *filepath = get_multiplePath(tokens[0]);
        // check from the 1st one to see whether we 
        // find a executable command
        tok_t *iter = filepath;
        while ((*iter) != NULL) {
          if (isExecutable(*iter)) {
            execv(*iter, &tokens[0]);
          }
          iter++;
        }
        perror("Command line not found\n");
        exit(1);
        // execv(tokens[0], &tokens[0]);
      } else if (cpid > 0) {
        // in parents branch
        waitpid(cpid, &status, 0);
      } else {
        // for failed
        perror("Fork failed");
        exit(1);
      }
       
      // fprintf(stdout, "This shell doesn't know how to run programs.\n");
    }
    // clean the garbage
    free_toks(tokens);
    freeln(input_bytes);

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);
  }

  return 0;
}
