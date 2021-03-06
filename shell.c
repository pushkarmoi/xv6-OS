#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define MAXARGS 10
#define CMD_LEN 150
#define ALIAS_KEY_LEN 30

/*
 *
 * Handling aliases.
 *
 */

typedef struct alias{
	char key[ALIAS_KEY_LEN];
	char value[CMD_LEN];
	struct alias* next;
} Alias;

Alias* alias_head = NULL;

int add_alias(const char* key, const char* value){
	Alias* new = malloc(sizeof(Alias));
	if(!new)
		return -1;

	strcpy(new->key, key);
	strcpy(new->value, value);
	new->next = NULL;

	if(alias_head){
		Alias* temp = alias_head;
		while(temp->next)
			temp = temp->next;
		temp->next = new;
	}else{
		alias_head = new;
	}
	return 0;
}

void del_alias(const char* key){
	Alias* this = alias_head;
	if(!this)
		return;

	if(strcmp(this->key, key) == 0){
		alias_head = alias_head->next;
		free(this);
		return;
	}

	while(this->next){
		if(strcmp(this->next->key, key) == 0){
			// MATCH!
			Alias* t = this->next;
			this->next = this->next->next;
			free(t);
			return;
		}else{
			this = this->next;
		}
	}
}


void close_alias(){
	// WRITE THE LINKED LIST BACK TO FILE
	//const char* filename = "alias.data";
	//FILE* alias_fh = fopen(filename, "w");
	//if(alias_fh){
		// add a \n at the end of everything you write. dont Remove the null character (automatically done).
		while(alias_head){
			Alias* temp = alias_head;

			/*
			char buf[CMD_LEN + 1];

			strcpy(buf, alias_head->key);
			buf[strlen(buf)] = '\n';
			buf[strlen(buf) + 1] = '\0';
			fputs(buf, alias_fh);

			strcpy(buf, alias_head->value);
			buf[strlen(buf)] = '\n';
			buf[strlen(buf) + 1] = '\0';
			fputs(buf, alias_fh);*/

			alias_head = alias_head->next;
			free(temp);
		}
		//fclose(alias_fh);
	//}
}

char* resolve_alias(char* key){
	Alias* index = alias_head;// CTRL+D
	while(index){
		if (strcmp(index->key, key) == 0){
			return index->value;
		}
		index = index->next;
	}
	return NULL;
}

char* search_replace(char* command){
	// receives something like:  "  cat -u file1| grep hello; top; foobar\n\0"
	//printf("%s", command);
	char result[CMD_LEN];
	strcpy(result, "");

	const char* whitespace = " \t";
	const char* tokens = " \t\n\r<>|;&";
	const char* cmdsep = "|;&\n"; // add \n if not working

	char* start = command;
	char* endofstring = start + strlen(command);
	char subcmd[CMD_LEN];

	while(start < endofstring){
		//printf("****\n");
		while((start < endofstring) && strchr(whitespace, *start)){
			char temp[2] = {*start, '\0'};
			strcat(result, temp);
			start++;
		}
		char* end = start + 1;
		while((end < endofstring) && !strchr(tokens, *end))
			end++;

			memcpy(subcmd, start, end - start);
			subcmd[end - start] = '\0';
			// resolve for alias -- subcmd.
			char* alias =  resolve_alias(subcmd);
			if(alias){
				strcpy(subcmd, alias);
			}
			strcat(result, subcmd);	// adding the command to the result
			//char temp[2] = {*end, '\0'};
			//strcat(result, temp);

		// this code is buggy  .>.
		//end += 1;
		start = end;
		while((end < endofstring) && !strchr(cmdsep, *end)){
			end++;
		}

		end += 1;
		if (end <=endofstring){
			memcpy(subcmd, start, end - start);
			subcmd[end-start] = '\0';
			strcat(result, subcmd);
		}
		start = end;
	}

	strcpy(command, result);
	return command;
}


/*
 *
 *
 * Implementing non-buffered input from console
 *
 *
 */

static struct termios old, new;

/* Initialize new terminal i/o settings */
void initTermios(int echo)
{
  tcgetattr(0, &old); /* grab old terminal i/o settings */
  new = old; /* make new settings same as old settings */
  new.c_lflag &= ~ICANON; /* disable buffered i/o */
  new.c_lflag &= echo ? ECHO : ~ECHO; /* set echo mode */
  tcsetattr(0, TCSANOW, &new); /* use these new terminal i/o settings now */
}

/* Restore old terminal i/o settings */
void resetTermios()
{
  tcsetattr(0, TCSANOW, &old);
}

/* Read 1 character - echo defines echo mode */
char getch_(int echo)
{
  char ch;
  initTermios(echo);
  ch = getchar();
  resetTermios();
  return ch;
}

/* Read 1 character without echo */
char getch(void)
{
  return getch_(0);
}


/*
 *
 * COMMAND HISTORY
 *
 *
 */

// COMMAND HISTORY STACK. Implemented as a linked-list
typedef struct node{
	char command[CMD_LEN];
	struct node* next;
	struct node* prev;
} Node;

typedef struct stack{
	Node* current;
	Node* head;
} Stack;

void stackinit(Stack* commandstack){
	commandstack->head = NULL;
	commandstack->current = NULL;
}

void releasemem(Stack* commandstack){
	Node* temp = commandstack->head;
	while(temp){
		Node* nxt = temp->prev;
		free(temp);
		temp = nxt;
	}
	free(commandstack);
}


char* getprevious(Stack* commandstack){
	if(!commandstack->current){
		if(!commandstack->head)
			return NULL;

		commandstack->current = commandstack->head;
	}else{
		if(!commandstack->current->prev)
			return NULL;

		commandstack->current = commandstack->current->prev;
	}
	return commandstack->current->command;
}

char* getnext(Stack* commandstack){
	if(!commandstack->current)
		return NULL;

	char* empty = "";
	if(!commandstack->current->next){
		commandstack->current = NULL;
		return empty;
	}

	commandstack->current = commandstack->current->next;
	return commandstack->current->command;
}


int addcommand(Stack* commandstack, const char* command){

	commandstack->current = NULL;   // reset current.
	if (strcmp(command, "") == 0) // don't add empty characters to history
		return 0;

	Node* new = malloc(sizeof(Node));
	if (!new)
		return -1;
	strcpy(new->command, command);

	if(commandstack->head){
		commandstack->head->next = new;
		new->prev = commandstack->head;
	}

	commandstack->head = new;
	return 0;
}

struct cmd {
  int type;          //  ' ' (exec), | (pipe), '<' or '>' for redirection, ';' for list command, '&' for back command
};

struct execcmd {
  int type;              // ' '
  char *argv[MAXARGS];   // arguments to the command to be exec-ed
};

struct backcmd {
  int type;              // '&'
  struct cmd* cmd;
};

struct redircmd {
  int type;          // < or >
  struct cmd *cmd;   // the command to be run (e.g., an execcmd)
  char *file;        // the input/output file
  int mode;          // the mode to open the file with
  int fd;            // the file descriptor number to use for the file
};

struct pipecmd {
  int type;          // |
  struct cmd *left;  // left side of pipe
  struct cmd *right; // right side of pipe
};

struct listcmd {
  int type;          // ;
  struct cmd *left;  // left side of ;
  struct cmd *right; // right side of ;
};

int fork1(void);  // Fork but exits on failure.
struct cmd *parsecmd(char*);

// Execute cmd.  Never returns.
void runcmd(struct cmd *cmd)
{
  int p[2], r;
  struct execcmd *ecmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;
  struct listcmd* lcmd;
  struct backcmd* bcmd;

  if(cmd == 0)
    exit(0);

  switch(cmd->type){
  default:
    fprintf(stderr, "unknown runcmd\n");
    exit(-1);

  case ' ':
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit(0);
    // argv[0] is the name of the command.

    // check in the alias table
    // format> number of rows, 1st line name, 2nd line is

    execvp(ecmd->argv[0], ecmd->argv);
    fprintf(stderr, "%s: couldn't be completed.\n", ecmd->argv[0]);	// should not be executed
    break;

  case '&':
	bcmd = (struct backcmd*)cmd;
	if(fork1() == 0){
		runcmd(bcmd->cmd);
		fprintf(stderr, "background job couldn't be completed.\n");
	}
	break;

  case '>':
  case '<':
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    const int permissions = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;	// rw, rw, r	// used whenever a new file is created with redirection
    rcmd->fd = open(rcmd->file, rcmd->mode, permissions);
    runcmd(rcmd->cmd);
    fprintf(stderr, "redirection couldn't be completed.\n");
    break;

  case ';':
	 lcmd = (struct listcmd*)cmd;
	 if (fork1() == 0){
		 struct cmd* leftcmd = lcmd->left;
		 runcmd(leftcmd);
	 }else{
		 wait(&r);
		 struct cmd* rightcmd = lcmd->right;
		 runcmd(rightcmd);
	 }

	 break;

  case '|':

	    pcmd = (struct pipecmd*)cmd;
	    pipe(p);

	    if(fork1() == 0){	// make left command run as a child
	    	struct cmd *leftcmd = pcmd->left;
	    	close(1);
	    	dup(p[1]);
	    	close(p[0]);  // cannot write till the read end of pipe is open.
	    	runcmd(leftcmd);
	    }

	    if (fork1() == 0){
	    	struct cmd *rightcmd = pcmd->right;
	    	close(0);
	    	dup(p[0]);
	    	close(p[1]);  // cannot read till the write end of pipe is open
	    	runcmd(rightcmd);
	    }

	    close(p[0]);
	    close(p[1]);
	    wait(&r);
	    wait(&r);

	    break;
	  	/*
	    pcmd = (struct pipecmd*)cmd;
	    pipe(p);

	    if(fork1() == 0){	// make left command run as a child
	    	struct cmd *leftcmd = pcmd->left;
	    	close(1);
	    	dup(p[1]);
	    	close(p[0]);  // cannot write till the read end of pipe is open.
	    	runcmd(leftcmd);
	    }else {
	    	struct cmd *rightcmd = pcmd->right;
	    	close(0);
	    	dup(p[0]);
	    	close(p[1]);  // cannot read till the write end of pipe is open
	    	runcmd(rightcmd);
	    }
	    break;

	    */

  }
  exit(0);
}

void safeexit(Stack* commandstack){
	  releasemem(commandstack);
	  close_alias();
	  exit(0);
}


int getcmd(char *buf, int nbuf, char* pwd, char* home, Stack* commandstack)
{
  int i;

  memset(buf, 0, nbuf);
  const int isterminal = isatty(fileno(stdin));
  if (!isterminal){
	  fgets(buf, nbuf, stdin);
	  if(buf[0] == 0) // EOF
	    return -1;
	  return 0;
  }

  // A TERMINAL  >>

  // **********************************  ABOUT displaying the path
  char display[1024];
  strcpy(display, pwd);
  if(strlen(display) >= strlen(home)){
	  // check for occurrence
	  int match = 1;
	  for(i = 0; i < strlen(home); i++){
		  if (display[i] != home[i]){
			  match = 0;
			  break;
		  }
	  }
	  if (match == 1){
		  memset(display, 0, 1024);
		  display[0] = '~';
		  strcpy(display+1, pwd + strlen(home));
	  }
  }


  fprintf(stdout, "238P:%s$ ", display);
  int charcount = 0;
  while(charcount < (nbuf - 1)){

	  int c = getch();

	  if (c == EOF){
		  safeexit(commandstack);
		  break;
	  }

	  if (c == '\t'){	// TABS are disabled
		  continue;
	  }

	  if (c == '\n'){
		  buf[charcount] = c;
		  charcount++;
		  buf[charcount] = '\0';
		  putchar('\n');
		  break;
	  }

	  if (c == 127){ // handle backspace
		  if (charcount > 0){
			  fprintf(stdout, "\b \b");
			  charcount--;
			  buf[charcount] = 0;
		  }
		  continue;
	  }

	  int arrowkey = 0;
	  if(c == 27){
		  if(getch() == '['){
			  arrowkey = 1;
		  }
	  }

	  if (arrowkey == 1){
		  int key = getch();

		  char* command;
		  if (key == 'A'){
			  command = getprevious(commandstack);
		  }else if(key == 'B'){
			  command = getnext(commandstack);
		  }else {
			  // LEFT AND RIGHT arrows
			  continue;
		  }

		  if (command){
			  // erase previous chars, add new chars to screen
			  for(i = 0; i < charcount; i++)
				  fprintf(stdout, "\b \b");

			  // add new chars to screen AND buffer
			  charcount = 0;
			  for(i = 0; i < strlen(command); i++){
				  putchar(command[i]);
				  buf[charcount] = command[i];
				  charcount++;
			  }
		  }


	  }else {
		  // NORMAL COMMAND
		  buf[charcount] = c;
		  charcount++;
		  putchar(c);
	  }

  }
  // end of reading characters. (Reached EOF or newline).

  buf[charcount] = '\0';
  if(buf[0] == 0) // EOF
    return -1;

  return 0;
}

int main(void)
{
  // TERMINAL init
  //initTermios(0);
  static char buf[CMD_LEN];
  int r;

  // STACK HISTORY init
  Stack* commandstack = malloc(sizeof(Stack));
  stackinit(commandstack);

  // DIRECTORY BEAUTIFICATION init
  char pwd[1024];
  char *homed;
  homed = getenv("HOME");
  getcwd(pwd, sizeof(pwd));

  // ALIAS init
  //open_alias();

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf), pwd, homed, commandstack) >= 0){

	// add \n if not present (for ./a.out < commands)
	if(buf[strlen(buf) - 1] != '\n'){
		int index = strlen(buf) - 1;
		buf[index] = '\n';
		buf[index + 1] = '\0';
	}

	// resolve for aliases
	search_replace(buf);

	// COMMAND HISTORY [buf] is the command. contains a trailing newline character. ALSO NULL TERMINATED. So,    ls\n\0
	char copy[strlen(buf) + 10];
	strcpy(copy, buf);
	copy[strlen(copy) - 1] = '\0';
	addcommand(commandstack, copy);
	// ********** added the command successfully

	char* exit_cmd = "exit";
	char* alias_cmd = "alias ";
	char* unalias_cmd = "unalias ";
	char* cd_cmd = "cd ";

	if(buf[0] == '#'){
		continue;
	}else if(strlen(buf) >= 4 && memcmp(exit_cmd, buf, 4) == 0){
		safeexit(commandstack);
	}else if(strlen(buf) > 6 && memcmp(alias_cmd, buf, 6) == 0){
		// ALIAS
		char key[ALIAS_KEY_LEN];
		char value[CMD_LEN];

		char* s = strchr(buf, ' ') + 1;
		char* e = strchr(buf, '=');

		int len = e - s;  // number of chars in key
		memcpy(key, s, len);
		key[len] = '\0';

		s = strchr(buf, '\'') + 1;
		e = strchr(s, '\'');

		len = e - s;
		memcpy(value, s, len);
		value[len] = '\0';

		add_alias(key, value);

	}else if(strlen(buf) > 8 && memcmp(unalias_cmd, buf, 8) == 0){
		// UNALIAS

		char key[ALIAS_KEY_LEN];

		char* s = strchr(buf, ' ') + 1;
		char* e = strchr(buf, '\n');

		int len = e - s;
		memcpy(key, s, len);
		key[len] = '\0';

		del_alias(key);

	}else if(strlen(buf) > 3 && memcmp(cd_cmd, buf, 3) == 0){
		// CD
	      // DIRECTORY CHANGE.

	      buf[strlen(buf)-1] = 0;  // chop the \n
	      char* home = "~";
	      if(strcmp(home, buf+3) == 0){
	    	  strcpy(buf+3, homed);
	      }
	      if(chdir(buf+3) < 0)
	        fprintf(stderr, "cannot cd %s\n", buf+3);
	      else{
	    	getcwd(pwd, sizeof(pwd));
	      }
	}else{
		// normal exec
        if(fork1() == 0)
        	runcmd(parsecmd(buf));
        wait(&r);
	}

  }

  safeexit(commandstack);
  //exit(0);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    perror("fork");
  return pid;
}

struct cmd* execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = ' ';
  return (struct cmd*)cmd;
}

struct cmd* redircmd(struct cmd *subcmd, char *file, int type)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = type;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->mode = (type == '<') ?  O_RDONLY : O_WRONLY|O_CREAT|O_TRUNC;
  cmd->fd = (type == '<') ? 0 : 1;
  return (struct cmd*)cmd;
}

struct cmd* pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = '|';
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd* listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = ';';
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}


struct cmd* backcmd(struct cmd *command)
{
  struct backcmd *cmd;
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = '&';
  cmd->cmd = command;
  return (struct cmd*)cmd;
}


// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>;&";

int gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '&':
  case ';':
  case '|':
  case '<':
    s++;
    break;
  case '>':
    s++;
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;

  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parselist(char**, char*);
struct cmd *parseback(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd* parseredirs(struct cmd *, char **, char *);

// make a copy of the characters in the input buffer, starting from s through es.
// null-terminate the copy to make it a string.
char *mkcopy(char *s, char *es)
{
  int n = es - s;
  char *c = malloc(n+1);
  assert(c);
  strncpy(c, s, n);
  c[n] = 0;
  return c;
}

struct cmd* parsecmd(char *start)
{
  char *end;
  struct cmd *cmd;

  end = start + strlen(start);
  cmd = parseline(&start, end);   // the CRUX.

  peek(&start, end, "");
  if(start != end){
    fprintf(stderr, "leftovers: %s\n", start);
    exit(-1);
  }
  return cmd;
}

struct cmd* parseline(char **start, char *end)
{
  struct cmd *cmd;
  cmd = parselist(start, end);
  return cmd;
}

struct cmd* parselist(char **start, char *end){
	struct cmd* cmd;
	cmd = parseback(start, end);
	if(peek(start, end, ";")){
	  gettoken(start, end, 0, 0);
	  cmd = listcmd(cmd, parselist(start, end));
	}
	return cmd;
}

struct cmd* parseback(char **start, char *end){
	struct cmd* cmd;
	cmd = parsepipe(start, end);
	while(peek(start, end, "&")){
	  gettoken(start, end, 0, 0);
	  cmd = backcmd(cmd);
	}
	return cmd;
}

struct cmd* parsepipe(char **start, char *end)
{
  struct cmd *cmd;
  cmd = parseexec(start, end);
  if(peek(start, end, "|")){
    gettoken(start, end, 0, 0);
    cmd = pipecmd(cmd, parsepipe(start, end));
  }
  return cmd;
}

struct cmd* parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|;&")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a') {
      fprintf(stderr, "syntax error\n");
      exit(-1);
    }
    cmd->argv[argc] = mkcopy(q, eq);
    argc++;
    if(argc >= MAXARGS) {
      fprintf(stderr, "too many args\n");
      exit(-1);
    }
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  return ret;
}

struct cmd* parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a') {
      fprintf(stderr, "missing file for redirection\n");
      exit(-1);
    }
    switch(tok){
    case '<':
      cmd = redircmd(cmd, mkcopy(q, eq), '<');
      break;
    case '>':
      cmd = redircmd(cmd, mkcopy(q, eq), '>');
      break;
    }
  }
  return cmd;
}
