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

void
list_datasets() {
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

  while ((d = readdir(dir))) {
    if (strchr(d->d_name, '~')) continue;
    if (strncmp(d->d_name, "0Logfile.", 9) != 0) continue;
    if (strspn(d->d_name+9, "0123456789.") != strlen(d->d_name+9)) continue;

    found++;
    char *scriptname = get_envvar("SCRIPT_NAME");
    printf("%s -- <a href=%s%s>raw</a> / <a href=%s%s/json>json</a> / <a href=%sbreath_plot?i=%s>Breath Plot</a><br>",
	   d->d_name+9, scriptname, d->d_name+9, scriptname, d->d_name+9, scriptname, d->d_name+9);
  }
  if (!found)
    printf("No data sets available");
  closedir(dir);
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
      printf("{ \"event\": \"M\",");
      char *v = strtok(line, ":"); // skip timestamp
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

  if (strlen(ipaddr) && strspn(ipaddr, "1234567890.") == strlen(ipaddr)) {
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
  }
  exit(0);
}
