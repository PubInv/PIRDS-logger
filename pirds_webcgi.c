/************************************
 This is a simple c based cgi program
 to read, interpret and send VentMon
 data.
         Geoff

 Written by Geoff Mulligan @2020

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

 Modified by Robert L. Read @2021
 Copyright 2021 Public Invention
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
#include <assert.h>
#include "PIRDS.h"


#define EVARSIZE 512
struct {
  char *name;
  char value[EVARSIZE];
} evars[] = {
  // Rob is experimenting with having an enivornment variable to set path...
  "PIRDS_WEBCGI", "",
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


// Until now, we have always used the "current directory".
// However, to have volumes work in Docker and retian files,
// we need to be able to set this, for example to "/data".
char *DIR_NAME = ".";


// Queries supported:
// json?n=XXXX -- means returns the most read XXXX samples in
// Json format.
// json?n=XXXX&t='UTC' -- means treutn XXXX samples starting at
// time UTC

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

  DIR *dir = opendir(DIR_NAME);
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

void find_line_from_time(FILE *fp, time_t epoch_time_start) {
  fseek(fp,0,SEEK_SET);
  char *line = NULL;
  size_t c = 0;
  while (getline(&line, &c, fp) > 0) {
      char *v = strtok(line, ":"); // skip timestamp
      // We can rely on this as a time stamp
      long epoch = atoi(v);
      if (epoch > epoch_time_start)
        return;
  }
}

// https://github.com/abejfehr/URLDecode/blob/master/urldecode.h

/* Function: urlDecode */
char *urlDecode(const char *str) {
  int d = 0; /* whether or not the string is decoded */

  char *dStr = malloc(strlen(str) + 1);
  char eStr[] = "00"; /* for a hex code */

  strcpy(dStr, str);

  while(!d) {
    d = 1;
    int i; /* the counter for the string */

    for(i=0;i<strlen(dStr);++i) {

      if(dStr[i] == '%') {
        if(dStr[i+1] == 0)
          return dStr;

        if(isxdigit(dStr[i+1]) && isxdigit(dStr[i+2])) {

          d = 0;

          /* combine the next to numbers into one */
          eStr[0] = dStr[i+1];
          eStr[1] = dStr[i+2];

          /* convert it to decimal */
          long int x = strtol(eStr, NULL, 16);

          /* remove the hex */
          memmove(&dStr[i+1], &dStr[i+3], strlen(&dStr[i+3])+1);

          dStr[i] = x;
        }
      }
    }
  }

  return dStr;
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
  asprintf(&fname, "%s/0Logfile.%s", DIR_NAME,ipaddr);
  //  fprintf(stderr,"fname = %s\n",fname);
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

  char *query;
  char *tokens;
  char *p;
  query = strdup (qs);  /* duplicate array, &array is not char** */
  tokens = query;
  p = query;
  //  printf("query %s\n",query);
  struct tm tm = {0};
  int time_found = 0;
  while ((p = strsep (&tokens, "&\n"))) {
    char *var = strtok (p, "="),
      *val = NULL;
    if (var && (val = strtok (NULL, "="))) {
      //      printf("%s %s\n",var,val);
      if (!strcmp(var,"t")) {
        char *decode = urlDecode(val);
        //               printf("DECODE ==%s\n",decode);

        char *s = strptime(decode, "%a, %d %b %Y %H:%M:%S", &tm);
        //          printf("First unprocessed |%s|\n",s);
        //           printf("UTC time: %s", asctime(&tm));
        time_found = 1;
        time_t epoch_time_start = timegm ( &tm );
        //           printf("epoch %ld",(long) epoch_time_start);
        // This positions in the write spot...
        find_line_from_time(fp,epoch_time_start);
        free(decode);
      }
    } else {
      fputs ("<empty field>\n", stderr);
    }
  }

  if ((backlines == 0 || backlines > 1) && json)
    printf("[\n");

  // in fact from the QUERY_STRING we need to get both n=XX and t=YY

  int first = 1;
  char *line = NULL;
  size_t c = 0;
  int line_cnt = 0;
  while (getline(&line, &c, fp) > 0 &&
         (line_cnt < backlines)) {
    line_cnt++;
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
      // Note: This fundamentally should come form our main library to reduce duplication
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
  //  if ((backlines == 0 || backlines > 1) && json)
  if ((backlines == 0 || backlines > 1) && json)
    printf("]\n");

  free (query);

  fclose(fp);

  return;
}

// This function copied from: https://stackoverflow.com/questions/9210528/split-string-with-delimiters-in-c
char** str_split(char* a_str, const char a_delim)
{
    char** result    = 0;
    size_t count     = 0;
    char* tmp        = a_str;
    char* last_comma = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;

    /* Count how many elements will be extracted. */
    while (*tmp)
    {
        if (a_delim == *tmp)
        {
            count++;
            last_comma = tmp;
        }
        tmp++;
    }

    /* Add space for trailing token. */
    count += last_comma < (a_str + strlen(a_str) - 1);

    /* Add space for terminating null string so caller
       knows where the list of returned strings ends. */
    count++;

    result = malloc(sizeof(char*) * count);
    result[count-1] = 0;

    if (result)
    {
        size_t idx  = 0;
        char* token = strtok(a_str, delim);

        while (token)
        {
            assert(idx < count);
            *(result + idx++) = strdup(token);
            token = strtok(0, delim);
        }
    }

    return result;
}


int main() {
  cgienv_parse();
  // if the "PIRDS_WEBCGI" is set, we want to use it as DIR_NAME.
  char *pirds_webcgi = get_envvar("PIRDS_WEBCGI");
  if (strlen(pirds_webcgi)) {
    // I believe this is okay because these strings are constants
    DIR_NAME = pirds_webcgi;
    //    fprintf(stderr,"DIR_NAME = %s\n",DIR_NAME);
  } else {
    //    fprintf(stderr,"PIRDS_WEBCGI not found\n");
  }


  char *uri = get_envvar("REQUEST_URI");

  char *qs = get_envvar("QUERY_STRING");

  if (uri == NULL) {
    printf("Content-type: text/plain\n");
    printf("Access-Control-Allow-Origin: *\n");
    printf("\n");
    printf("Bad Request");
    exit(1);
  }

  // I think I want to make this code more robust so the url can be served
  // from different places.
  // The basic role is: If the last / sesparated token is of the form
  // d?.d?.d?.d? then we will NOT last the datasets. Otherwise we will.
  // If the form is /json/ipaddr", then we treat the type as JSON ("1") below.
  // Else we provide the raw data "0" below.

  char *path_for_uri = strdup(uri+1);

  // I want to process the URI without the QUERY_STRONG present.
  char** uri_tokens;
  uri_tokens = str_split(path_for_uri, '?');
  char uri_only[256];

  if (uri_tokens[0]) {
  strcpy(uri_only,uri_tokens[0]);
  // now we deallocate...

  // tokens[1] if it exist should be the query string...
  if (uri_tokens[1]) {
    assert(strcmp(uri_tokens[1],qs) == 0);
  }

  {
    size_t i;
    for (i = 0; *(uri_tokens + i); i++)
      {
        free(*(uri_tokens + i));
      }
  }
  } else {
    uri_only[0] = '\0';
  }

  char** tokens;
  tokens = str_split(uri_only, '/');

  // we need only the last token and (posibly) the penultimate token;
  char ult_token[256];
  char pen_token[256];
  ult_token[0] = '\0';
  pen_token[0] = '\0';
  if (tokens[0]) {
      size_t i;
      for (i = 0; *(tokens + i); i++)
        {
          strcpy(pen_token,ult_token);
          strcpy(ult_token,*(tokens + i));
          free(*(tokens + i));
        }
      free(tokens);
      if (strlen(ult_token)) {
        if (strlen(pen_token) && strcasecmp(ult_token, "json") == 0) {
          dump_data(pen_token, 1);
        } else {
          dump_data(ult_token, 0);
        }
      } else {
        printf("Content-type: text/plain\n");
        printf("Access-Control-Allow-Origin: *\n");
        printf("\n");
        printf("Bad Request");
        printf("%s",ult_token);
        exit(0);
      }
  } else {
    list_datasets();
    exit(0);
  }
}
