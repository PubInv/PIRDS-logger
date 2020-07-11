/************************************
 This is a simple c based cgi program
 to read, interpret and send VentMon
 data.
         Geoff

 Written by Geoff Mulligan @2020
***************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>

#define EVARSIZE 512
struct {
  char *name;
  char value[EVARSIZE];
} evars[] = {
  "AUTH_TYPE", "",
  "CONTENT_LENGTH", "",
  "CONTENT_TYPE", "",
  "DOCUMENT_ROOT", "",
  "GATEWAY_INTERFACE", "",
  "HTTP_ACCEPT", "",
  "HTTP_COOKIE", "",
  "HTTP_REFERER", "",
  "HTTP_USER_AGENT", "",
  "PATH_INFO", "",
  "PATH_TRANSLATED", "",
  "QUERY_STRING", "",
  "REMOTE_ADDR", "",
  "REMOTE_HOST", "",
  "REMOTE_IDENT", "",
  "REMOTE_USER", "",
  "REQUEST_METHOD", "",
  "REQUEST_URI", "",
  "SCRIPT_NAME", "",
  "SERVER_NAME", "",
  "SERVER_PORT", "",
  "SERVER_PROTOCOL", "",
  "SERVER_SOFTWARE", "",
  NULL, "THE END"
};

#define PATH_MAX 4096

void
cgienv_parse() {
  for (uint8_t i = 0; evars[i].name != NULL; i++) {
    char *e = getenv(evars[i].name);
    if (e)
      strncpy(evars[i].value, e, EVARSIZE);
    else
      evars[i].value[0] = '\0';
  }
}

char *
get_envvar(char *value) {
  for (uint8_t i=0; evars[i].name != NULL; i++) {
    if (strcasecmp(value, evars[i].name) == 0)
      return evars[i].value;
  }
  return NULL;
}

static inline int file_select(const struct dirent *d)
{
  return (strncmp(d->d_name, "0Logfile.", 9) == 0);
}

char *DIR_NAME = ".";
static inline int
sortbydatetime(const struct dirent **a, const struct dirent **b)
{
  int rval;
  struct stat sbuf1, sbuf2;
  char path1[PATH_MAX], path2[PATH_MAX];

  snprintf(path1, PATH_MAX, "%s/%s", DIR_NAME, (*a)->d_name);
  snprintf(path2, PATH_MAX, "%s/%s", DIR_NAME, (*b)->d_name);

  rval = stat(path1, &sbuf1);
  if (rval) {
    perror("stat");
    return 0;
  }
  rval = stat(path2, &sbuf2);
  if (rval) {
    perror("stat");
    return 0;
  }

  return sbuf1.st_mtime > sbuf2.st_mtime;
}

void list_datasets_by_time() {
  char *DIR_NAME = ".";
  char path[PATH_MAX];

  DIR *pdir;
  struct stat sbuf;
  struct dirent **pdirent;

  struct tm lt;
  char strdate[16];
  char strtime[16];

  int nfile = 0;

  pdir = opendir(DIR_NAME);
  if (!pdir) {
    perror("opendir");
    return;
  }

  int n = scandir(DIR_NAME, &pdirent, file_select, sortbydatetime);
  if (n == -1) {
    perror("scandir");
    return;
  }
  while (n--) {
    if (!((strcmp(pdirent[n]->d_name, ".") == 0) ||
          (strcmp(pdirent[n]->d_name, "..") == 0)))
      {
        snprintf(path, PATH_MAX, "%s/%s", DIR_NAME, pdirent[n]->d_name);
        int rval = stat(path, &sbuf);
        if (rval) {
          perror("stat");
          return;
        }
        localtime_r(&sbuf.st_mtime, &lt);
        memset(strtime, 0, sizeof(strtime));
        strftime(strtime, sizeof(strtime), "%H:%M:%S", &lt);
        memset(strdate, 0, sizeof(strdate));
        strftime(strdate, sizeof(strdate), "%F", &lt);
        char *scriptname = get_envvar("SCRIPT_NAME");
        printf("%s -- <a href=%s%s>raw</a> / <a href=%s%s/json>json</a> / <a href=%sbreath_plot?i=%s>Breath Plot</a><br>",
               pdirent[n]->d_name+9, scriptname, pdirent[n]->d_name+9, scriptname, pdirent[n]->d_name+9, scriptname, pdirent[n]->d_name+9);
      }
    free(pdirent[n]);
  }
  free(pdirent);
}

void
list_datasets() {
  char *DIR_NAME = ".";




  DIR *dir = opendir(".");
  if (!dir) {
    printf("Content-type: text/plain\n");
    printf("Access-Control-Allow-Origin: *\n");
    printf("\n");
    printf("Can't open directory");
    return;
  }

  struct dirent *d = NULL;
  uint8_t found = 0;
  printf("Content-type: text/html\n");
  printf("Access-Control-Allow-Origin: *\n");
  printf("\n");
  closedir(dir);

  list_datasets_by_time();
}

void find_back_lines(FILE *fp, int count) {
  int lc = count + 1;
  unsigned long pos = 0;

  while (lc) {
    if (fseek(fp, -pos, SEEK_END) == -1) {
      rewind(fp);
      break;
    }
    if (fgetc(fp) == '\n')
      lc--;
    pos++;
  }

  return;
}

void
dump_data(char *ipaddr, int json) {
  if (json)
    printf("Content-type: application/json\n");
  else
    printf("Content-type: text/plain\n");
  printf("Access-Control-Allow-Origin: *\n");
  printf("\n");

  char *fname = NULL;
  asprintf(&fname, "0Logfile.%s", ipaddr);
  FILE *fp = fopen(fname, "r");
  if (fname) free(fname);
  if (!fp) {
    printf("No such dataset %s\n", ipaddr);
    return;
  }
  char *qs = get_envvar("QUERY_STRING");
  int backlines = 0;
  if (qs && strncmp(qs, "n=", 2) == 0) {
    backlines = atoi(qs+2);
    if (backlines > 0)
      find_back_lines(fp, backlines);
  }

  if ((backlines == 0 || backlines > 1) && json)
    printf("[\n");

  int first = 1;
  char *line = NULL;
  size_t c = 0;
  while (getline(&line, &c, fp) > 0) {
    if (!json)
      printf("%s", line);
    else {
      char *ptr;
      if ((ptr = strchr(line, '\r')) || (ptr = strchr(line, '\n')))
	*ptr = '\0';
      if (!first)
	printf(",\n");
      first = 0;
      //      printf("{ \"event\": \"M\",");
      char *v = strtok(line, ":"); // skip timestamp
      v = strtok(NULL, ":");
      // This part is for back-compatibility;
      // it should be removed when we have a chance
      if ((0 == strcmp(v,"P")) ||
          (0 == strcmp(v,"D")) ||
          (0 == strcmp(v,"F")) ||
          (0 == strcmp(v,"H")) ||
          (0 == strcmp(v,"G")) ||
          (0 == strcmp(v,"T")) ||
          (0 == strcmp(v,"A"))
          ) {
        printf("{ \"event\": \"M\",");
        //      v = strtok(NULL, ":");
        printf(" \"type\": \"%s\",", v);
        v = strtok(NULL, ":");
        printf(" \"loc\": \"%s\",", v);
        v = strtok(NULL, ":");
        printf(" \"num\": %s,", v);
        v = strtok(NULL, ":");
        printf(" \"ms\": %s,", v);
        v = strtok(NULL, ":");
        printf(" \"val\": %s }", v);
      } else if (0 == strcmp(v,"M")) {
        printf("{ \"event\": \"%s\",",v);
        v = strtok(NULL, ":");
        printf(" \"type\": \"%s\",", v);
        v = strtok(NULL, ":");
        printf(" \"loc\": \"%s\",", v);
        v = strtok(NULL, ":");
        printf(" \"num\": %s,", v);
        v = strtok(NULL, ":");
        printf(" \"ms\": %s,", v);
        v = strtok(NULL, ":");
        printf(" \"val\": %s }", v);
      } else if (0 == strcmp(v,"E")) {
        // The only currently supported other format is "M"
        v = strtok(NULL, ":");
        if (0 == strcmp(v,"M")) {
            printf("{ \"event\": \"%s\",","E");
            printf(" \"type\": \"M\",");
            v = strtok(NULL, ":");
            printf(" \"ms\": %s,", v);
            v = strtok(NULL, ":");
            // Note: This string is already double quoted...
            // this is not good strong typing,
            // it should be improved.
            printf(" \"buff\": %s }", v);
        } else if (0 == strcmp(v,"C")) {
            printf("{ \"event\": \"%s\",","E");
            printf(" \"type\": \"C\",");
            v = strtok(NULL, ":");
            printf(" \"ms\": %s,", v);
            // Now get the rest of the string, as it is a complex date
            v = strtok(NULL, "\0");
            printf(" \"buff\": %s }", v);
        } else {
          printf("\"\"");
        }
      }
    }
  }
  if ((backlines == 0 || backlines > 1) && json)
    printf("]\n");
  fclose(fp);

  return;
}
int main() {
  cgienv_parse();

  char *uri = get_envvar("REQUEST_URI");
  if (uri == NULL) {
    printf("Content-type: text/plain\n");
    printf("Access-Control-Allow-Origin: *\n");
    printf("\n");
    printf("Bad Request");
    exit(1);
  }

  char *path = strdup(uri+1);
  //  char *path = strdup(uri+14);
  char *ptr;
  if ((ptr = strchr(path, '?')))
    *ptr = '\0';

  if (strlen(path) == 0 || strcmp(path, "/") == 0) {
    list_datasets();
    exit(0);
  }

  char *ipaddr = strtok(path, "/");
  char *type = strtok(NULL, "/");

  // Probably I will have to take the digits out of this explicitly
  if (strlen(ipaddr)
      //      && strspn(ipaddr, "1234567890.") == strlen(ipaddr)
      ) {
    if (type && strcasecmp(type, "json") == 0) {
      dump_data(ipaddr, 1);
    } else {
      dump_data(ipaddr, 0);
    }
  } else {
    printf("Content-type: text/plain\n");
    printf("Access-Control-Allow-Origin: *\n");
    printf("\n");
    printf("Bad Request");
    printf("%s",ipaddr);
  }
  exit(0);
}
