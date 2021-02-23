/************************************
MIT License

Copyright (c) 2020 Public Invention

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

 This is a datalogger for the VentMon project
 It can be started to listen for tcp connections or
 ucp packets. The packets must be formatted in the PIRDS
 format. The data is logged to simple unix file in the
 current directory based on the ip address of the sender.
 These means that there will be a collision if there are
 multiple senders behind a single nat.
 I'll fix this later.
         Geoff

 Written by Geoff Mulligan @2020
 Modified by Robert L. Read May 2020, to use PIRDS library

***************************************/

#define VERSION 1.7

#define _GNU_SOURCE
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#if __linux__
#include <sys/prctl.h> // prctl(), PR_SET_PDEATHSIG
#endif
#include <stdbool.h>
#include "PIRDS.h"


#define SAVE_LOG_TO_FILE "SAVE_LOG_TO_FILE:"

// Message Types
struct {
  char type;
  char *value;
} message_types[] = {
  '{', "JSON DATA",
  '!', "Emergency",
  'A', "Alarm",
  'B', "Battery",
  'C', "Control",
  'D', "Unknown",
  'E', "Event",
  'F', "Failure",
  'G', "Unknown G",
  'H', "Unknown H",
  'I', "Unknown I",
  'J', "Unknown J",
  'K', "Unknown K",
  'L', "Limits",
  'M', "Measurement",
  'N', "Unknown N",
  'O', "Unknown O",
  'P', "PARAMETERS",
  'Q', "Unknown Q",
  'R', "Unknown R",
  'S', "Assertion",
  'T', "Unknown T",
  'U', "Unknown U",
  'V', "Unknown V",
  'W', "Unknown W",
  'X', "Unknown X",
  'Y', "Unknown Y",
  'Z', "Unknown Z",
  '\0', "THE END"
};

#define UDP 0
#define TCP 1

uint8_t gDEBUG = 1;

#define USESELECT
#ifdef USESELECT
#define DATA_TIMEOUT 60 // for TCP connections
#endif

#define BSIZE 65*1024
uint8_t buffer[BSIZE];

#define ONE_EVENT_BUFFER_SIZE 1024

void handle_udp_connx(int listenfd);
void handle_tcp_connx(int listenfd);

int
handle_event(uint8_t *buffer, int fd, struct sockaddr_in *clientaddr, char *peer, bool mark_minute);

FILE *gFOUTPUT;

// We will keep a count of the number of minutes
// since the UNIX epoch (seconds/60).
// When this changes, we will set a mark for the
// first child process to inject a "clock" event.
unsigned long epoch_minute;

// We need to associate the current "peer" string
// with the current milliseconds in the stream
// in order to be able to inject clockevents correctly.
// One way to do this is to look backwards in the
// current file, which will fail if we are just beginning.

int main(int argc, char* argv[]) {
  uint8_t mode = UDP;

  int opt;
  while ((opt = getopt(argc, argv, "Dt")) != -1) {
    switch (opt) {
    case 'D': gDEBUG++; break;
    case 't': mode = TCP; break;
    default: printf("Usage: %s [-D] [-t] [port]\n", argv[0]);
      exit(1);
    }
  }

  gFOUTPUT = stderr;
  if (gDEBUG > 1)
    gFOUTPUT = stdout;

 char *port = "6111";
  if (mode == TCP)
    port = "6110";

  if (optind < argc) {
    port = argv[optind];
  }

  if (gDEBUG)
    fprintf(gFOUTPUT, "%s Server started %son port %s%s\n",
	    mode == TCP ? "TCP":"UDP",
	    "\033[92m", port, "\033[0m");

  // getaddrinfo for host
  struct addrinfo hints, *res;
  memset (&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  if (mode == TCP)
    hints.ai_socktype = SOCK_STREAM;
  else
    hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;
  if (getaddrinfo( NULL, port, &hints, &res) != 0) {
    perror ("getaddrinfo() error");
    exit(1);
  }

  // socket and bind
  struct addrinfo *p;
  int listenfd;
  for (p = res; p != NULL; p = p->ai_next) {
    listenfd = socket (p->ai_family, p->ai_socktype, 0);
    if (listenfd == -1) continue;
    int option = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof option);
    if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0) break;
  }

  freeaddrinfo(res);

  if (p == NULL) {
    perror ("socket() or bind()");
    exit(1);
  }
  if (gDEBUG)
    fprintf(gFOUTPUT, "LOOP!\n");

  while (1) {
    if (mode == TCP)
      handle_tcp_connx(listenfd);
    else
      handle_udp_connx(listenfd);
  }
}

void
send_params(char *peer, char *addr) {
#if 0
  printf("HTTP/1.1 200 OK\n\n");
  printf("PARAM_WAIT: 300\n");
  fflush(stdout);
#endif
}

// For debugging...
void render_measurement(Measurement* m) {
  fprintf(stderr,"Measurement:\n" );
  fprintf(stderr,"Event %c\n", m->event);
  fprintf(stderr,"type %c\n", m->type);
  fprintf(stderr,"loc %c\n", m->loc);
  fprintf(stderr,"num %u\n", m->num);
  fprintf(stderr,"ms %u\n", m->ms);
  fprintf(stderr,"val %d\n", m->val);
}

/*
void
log_measurement_bytecode(char *peer, void *buff, bool limit) {
  struct __attribute__((__packed__)) measurement_t *measurement = (struct measurement_t *) buff;
*/

FILE* open_log_file(char *peer) {
  // xxx need file locking
  char fname[30];
  strcpy(fname, "0Logfile.");
  strcpy(fname + 9, peer);

  FILE *fp = fopen(fname, "a");
  return fp;
}

/**
 * Copy content from one file to other file.
 */
int
copy_file(char path_to_read_file[], char path_to_write_file[])
{
    char chr;
    FILE *stream_for_write, *stream_for_read;

    if ((stream_for_write = fopen(path_to_write_file, "w")) == NULL) {
        fprintf(stderr, "%s: %s\n", "Impossible to create a file", path_to_write_file);
        return -1;
    }

    if ((stream_for_read = fopen(path_to_read_file, "r")) == NULL) {
        fprintf(stderr, "%s: %s\n", "Impossible to read a file", path_to_read_file);
        return -1;
    }

    while (!feof(stream_for_read)) {
      char chr = fgetc(stream_for_read);
      fputc(chr, stream_for_write);
    }

    fclose(stream_for_write);
    fclose(stream_for_read);
    return 0;
}

void copy_log_file_to_name(char* peer,char* name) {

  char fname[30];
  int ret;
  strcpy(fname, "0Logfile.");
  strcpy(fname + 9, peer);

  char cmd[256];

  sprintf(cmd,"mv %s %s",fname,name);
  system(cmd);
  fprintf(gFOUTPUT,"old name %s, new name %s",fname,name);
  /* ret =   rename(fname,name); */
  /* if(ret == 0) { */
  /*     fprintf(gFOUTPUT,"File renamed successfully"); */

  /*  } else { */
  /*     fprintf(gFOUTPUT, "Error: unable to rename the file"); */
  /*  } */
  //  copy_file(fname,name);
}

void mark_minute_into_stream(uint32_t cur_ms, int fd, struct sockaddr_in *clientaddr, char *peer) {
    // Here whenever a new minute ticks over we output a new Clock event
    // I can
    char iso_time_string[256];
    time_t now;
    time(&now);

    struct tm *ptm = gmtime(&now);

    if (ptm == NULL) {
        puts("The gmtime() function failed");
    }
    sprintf(iso_time_string,"%s", asctime(ptm));
    // remove traling newline
    iso_time_string[strcspn(iso_time_string, "\n")] = 0;
    //    unsigned long cur_ms = get_most_recent_ms(peer);
    // This is a fake until I figure out how to get it out of the file!
    Message clockEvent = {
      'E','C',cur_ms,(uint8_t) strlen(iso_time_string)
    };
    strcpy(clockEvent.buff,iso_time_string);
    uint8_t lbuffer[263];
    fill_byte_buffer_message(&clockEvent,lbuffer,263);
    handle_event(lbuffer, fd, clientaddr, peer, false);
}

uint32_t
log_measurement_bytecode(char *peer, void *buff, bool limit) {
  Measurement measurement = get_measurement_from_buffer(buff,13);
  return log_measurement_bytecode(peer,&measurement,limit);
}

uint32_t
log_measurement_bytecode_from_measurement(char *peer, Measurement* measurement, bool limit) {

  FILE *fp = open_log_file(peer);
  if (!fp) return 0;

  fprintf(fp, "%lu:%c:%c:%c:%u:%u:%d\n", time(NULL),
            measurement->event,
            measurement->type, measurement->loc,
  	  measurement->num, measurement->ms, measurement->val);
  fclose(fp);
  return measurement->ms;
}

void get_timestamp( char *buffer, size_t buffersize ) {
   time_t rawtime;
   struct tm * timeinfo;
   time (&rawtime);
   timeinfo = localtime (&rawtime);
   strftime (buffer,16,"%G%m%d%H%M%S",timeinfo);
   puts(buffer);
}


uint32_t
log_event_bytecode_from_message(char *peer, Message* message, bool limit) {

  int n = strlen(SAVE_LOG_TO_FILE);
  if (strncmp(message->buff,SAVE_LOG_TO_FILE,n) == 0) {
    char *name = message->buff+n;
      char fname[256];
      char cname[256];
      strcpy(fname, "0Logfile.");
      strcpy(fname + 9, peer);
      strcpy(cname,fname);
      strcpy(fname + strlen(fname),".");
      strcpy(fname + strlen(fname),name);
      strcpy(fname + strlen(fname),".");
      get_timestamp(fname+strlen(fname),16);
      copy_log_file_to_name(peer,fname);
      //      remove(cname);
  } else {
    FILE *fp = open_log_file(peer);
    if (!fp) return 0;

    fprintf(fp, "%lu:%c:%c:%u:\"%s\"\n",
          time(NULL),
          message->event,
          message->type,
          message->ms,
          message->buff);
      fclose(fp);
  }
  return message->ms;
}

uint32_t log_event_bytecode(char *peer, void *buff, bool limit) {
  Message message = get_message_from_buffer(buff,263);
  return log_event_bytecode_from_message(peer,&message,limit);
}

void
log_json(char *peer, void *buff) {

  FILE *fp = open_log_file(peer);
  if (!fp) return;


  char *ptr = strchr((char *)buff, '\n');
  if (ptr) *ptr = '\0';
  if ((ptr = strchr((char*)buff, '\r')))
    *ptr = '\0';

  if (((char *)buff)[0] == '[') {
    fprintf(fp, "[ {\"TimeStamp\": %lu}, %s\n", time(NULL), (char *)buff+1);
  } else {
    fprintf(fp, "{\"TimeStamp\": %lu, %s\n", time(NULL), (char *)buff+1);
  }
  fclose(fp);
}
void print_message(Message message,bool limit);

void
print_event_bytecode(void *buff, bool limit) {
  char second_char = ((char *)buff)[1];
  if (second_char == 'M') {
    Message message = get_message_from_buffer(buff,263);
    print_message(message,limit);
  }
}

void print_message(Message message,bool limit) {
  fprintf(gFOUTPUT, "  MESSAGE||%s||\n", message.buff);
}

void print_measurement(Measurement* measurement,bool limit);

void
print_measurement_bytecode(void *buff, bool limit) {
  Measurement measurement = get_measurement_from_buffer(buff,13);
  print_measurement(&measurement,limit);
}

 void print_measurement(Measurement* measurement,bool limit) {
  //  render_measurement(&measurement);
  int v = (int) measurement->val;
  float fv = (float) v;

  switch (measurement->type) {
  case 'T':
    fprintf(gFOUTPUT, "  Temp%s %c%d (%u): %f C\n", limit ? "LIMIT" : "",
	   measurement->loc, measurement->num, measurement->ms,
            fv /100.0);
    break;
  case 'P':
    fprintf(gFOUTPUT, "  Pressure%s %c%d (%u): %f cm\n", limit ? "LIMIT" : "",
	   measurement->loc, measurement->num, measurement->ms,
            fv /100.0);
    break;
  case 'D':
    fprintf(gFOUTPUT, "  DifferentialPressure%s %c%d (%u): %f cm\n", limit ? "LIMIT" : "",
	   measurement->loc, measurement->num, measurement->ms,
            fv /10.0);
    break;
  case 'F':
    fprintf(gFOUTPUT, "  Flow%s %c%d (%u): %f l\n", limit ? "LIMIT" : "",
	   measurement->loc, measurement->num, measurement->ms,
            fv /100.0);
    break;
  case 'O':
    fprintf(gFOUTPUT, "  FractionalO2%s %c%d (%u): %f%%\n", limit ? "LIMIT" : "",
	   measurement->loc, measurement->num, measurement->ms,
            fv /100.0);
    break;
  case 'H':
    fprintf(gFOUTPUT, "  Humidity%s %c%d (%u): %f%%\n", limit ? "LIMIT" : "",
	   measurement->loc, measurement->num, measurement->ms,
	   fv/100.0);
    break;
  case 'V':
    fprintf(gFOUTPUT, "  Volume%s %c%d (%u): %d ml\n", limit ? "LIMIT" : "",
	   measurement->loc, measurement->num, measurement->ms,
	   v);
    break;
  case 'B':
    fprintf(gFOUTPUT, "  Breaths%s %c%d (%u): %d\n", limit ? "LIMIT" : "",
	   measurement->loc, measurement->num, measurement->ms,
	   v/10);
    break;
  case 'G':
    fprintf(gFOUTPUT, "  Gas%s %c%d (%u): %d\n", limit ? "LIMIT" : "",
	   measurement->loc, measurement->num, measurement->ms,
	   v);
    break;
  case 'A':
    fprintf(gFOUTPUT, "  Altitude%s %c%d (%u): %d m\n", limit ? "LIMIT" : "",
	   measurement->loc, measurement->num, measurement->ms,
	   v);
    break;
  default: fprintf(gFOUTPUT, "Invalid measurement type: %c\n",measurement->type);
  }
}

// I'm confused as to what this does! - rlr
void
print_json(void *buff) {
  char *ptr = strchr(buff, '\n');
  if (ptr) *ptr = '\0';
  if ((ptr = strchr(buff, '\r')))
    *ptr = '\0';
  fprintf(gFOUTPUT, "%s\n", (char *)buff);
}

#if 0
void zombie_hunter(int sig)
{
  int status;
  waitpid(pid, &status, 0);
  fprintf(gFOUTPUT, "Got status %d from child\n",status);
  finished=1;
}
#endif

// TODO: The use of mark_minute here is very confusing and duplicative;
// it should be extracted from the
int
handle_event(uint8_t *buffer, int fd, struct sockaddr_in *clientaddr, char *peer, bool mark_minute) {
  char *xbuffer = (char *) buffer;

  // TODO: This should be a a function...
  uint8_t x = 0;
  while (message_types[x].type != '\0') {
    if (buffer[0] == message_types[x].type) break;
    x++;
  }

  if (message_types[x].type == '\0') {
    if (gDEBUG)
      fprintf(gFOUTPUT, "  Invalid Message from buffer |%s|\n",buffer);
    return 0;
  }

  int flags = 0;
#if __linux__
  flags = MSG_CONFIRM;
#endif

  int8_t rvalue = 0;
  switch(message_types[x].type) {
  case '{':
    {
    // If this is an event or a measurmente, we want to log it
    // as bytes, just as if it came in as bytes. If it is not, we will
    // log it as JSON, assuming it is a comment.
    // we will want to call get_measurement_from_JSON here, unless
    // the the "event" type is an "M", in which case we get to do
    // "get_message_from_JSON". This is functionality we need to add to PIRDS
    // itself!

    char c = get_event_designation_char_from_json((const char *) buffer,ONE_EVENT_BUFFER_SIZE);
    switch (c) {
    case 'M': {
      int n = strlen((const char *)buffer);
      if (n >= ONE_EVENT_BUFFER_SIZE) {
        fprintf(gFOUTPUT, "INTERNAL ERROR, BUFFER LENGTH TOO HIGH\n");
        // I'm not sure what this means
        return 2;
      }
      //      char buff[1024]; // ugly
      //      strcpy(buff,(const char *) buffer);
      Measurement mp = get_measurement_from_JSON((char *) buffer,ONE_EVENT_BUFFER_SIZE);


      // TODO: Much of this code below is duplicated; I hate
      // duplication!!
      uint32_t ms = log_measurement_bytecode_from_measurement(peer, &mp, true);
      if (mark_minute) {
        mark_minute_into_stream(ms,fd,clientaddr,peer);
      }
      if (clientaddr)
        sendto(fd, "OK\n", 3, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
      else
        write(fd, "OK\n", 3);
      break;
    }
    case 'E': {
      Message msg = get_message_from_buffer(buffer,ONE_EVENT_BUFFER_SIZE);
      uint32_t ms = log_event_bytecode_from_message(peer, &msg, true);
      if (mark_minute) {
        mark_minute_into_stream(ms,fd,clientaddr,peer);
      }

      if (gDEBUG)
        print_event_bytecode(buffer, true);
      if (clientaddr)
        sendto(fd, "OK\n", 3, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
      else
        write(fd, "OK\n", 3);

      rvalue = 1;
      break;
    }
    case '\0':
      if (gDEBUG)
        fprintf(gFOUTPUT, "  Unknown %c Message\n", message_types[x].type);
      if (clientaddr)
        sendto(fd, "UNK\n", 4, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
      else
        write(fd, "UNK\n", 4);
      rvalue = 2;
      break;
    default:
      if (gDEBUG)
        fprintf(gFOUTPUT, "  Unknown %c Message\n", message_types[x].type);
      if (clientaddr)
        sendto(fd, "UNK\n", 4, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
      else
        write(fd, "UNK\n", 4);
      rvalue = 2;
      break;
    };

    //    log_json(peer, buffer);
    if (clientaddr)
      sendto(fd, "OK\n", 3, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
    else
      write(fd, "OK\n", 3);
    break;
    }
  case '!':
    if (gDEBUG)
      fprintf(gFOUTPUT, "  Emergency Message\n");
    if (clientaddr)
      sendto(fd, "NOP\n", 4, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
    else
      write(fd, "NOP\n", 4);
    rvalue = 1;
    break;
  case 'A':
    if (gDEBUG)
      fprintf(gFOUTPUT, "  Alarm Message\n");
    if (clientaddr)
      sendto(fd, "NOP\n", 4, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
    else
      write(fd, "NOP\n", 4);
    rvalue = 1;
    break;
  case 'B':
    if (gDEBUG)
      fprintf(gFOUTPUT, "  Battery Message\n");
    if (clientaddr)
      sendto(fd, "NOP\n", 4, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
    else
      write(fd, "NOP\n", 4);
    rvalue = 1;
    break;
  case 'C':
    if (gDEBUG)
      fprintf(gFOUTPUT, "  Control Message\n");
    if (clientaddr)
      sendto(fd, "NOP\n", 4, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
    else
      write(fd, "NOP\n", 4);
    rvalue = 1;
    break;
    /* The is an *E*vent */
  case 'E':
    {
      uint32_t ms = log_event_bytecode(peer, buffer, true);
      if (mark_minute) {
        mark_minute_into_stream(ms,fd,clientaddr,peer);
      }

    if (gDEBUG)
      print_event_bytecode(buffer, true);
    if (clientaddr)
      sendto(fd, "OK\n", 3, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
    else
      write(fd, "OK\n", 3);
    rvalue = 1;
    }
    break;
  case 'F':
    if (gDEBUG)
      fprintf(gFOUTPUT, "  Failure Message\n");
    if (clientaddr)
      sendto(fd, "NOP\n", 4, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
    else
      write(fd, "NOP\n", 4);
    rvalue = 1;
    break;
  case 'L':
    {
    uint32_t ms = log_measurement_bytecode(peer, buffer, true);
    if (mark_minute) {
      mark_minute_into_stream(ms,fd,clientaddr,peer);
    }
    if (gDEBUG)
      print_measurement_bytecode(buffer, true);
    if (clientaddr)
      sendto(fd, "OK\n", 3, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
    else
      write(fd, "OK\n", 3);
    }
    break;
  case 'M':
    {
    uint32_t ms = log_measurement_bytecode(peer, buffer, true);
    if (mark_minute) {
      mark_minute_into_stream(ms,fd,clientaddr,peer);
    }
    if (gDEBUG)
      print_measurement_bytecode(buffer, false);
    if (clientaddr)
      sendto(fd, "OK\n", 3, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
    else
      write(fd, "OK\n", 3);
    }
    break;
  case 'P':
    if (gDEBUG)
      fprintf(gFOUTPUT, "  Param request\n");
    if (clientaddr)
      sendto(fd, "NOP\n", 4, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
    else
      write(fd, "NOP\n", 4);
    rvalue = 1;
    break;
  case 'S':
    if (gDEBUG) {
      fprintf(gFOUTPUT, "  aSsertion Message\n");
    }
    if (clientaddr)
      sendto(fd, "NOP\n", 4, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
    else
      write(fd, "NOP\n", 4);
    rvalue = 1;
    break;
  default:
    if (gDEBUG)
      fprintf(gFOUTPUT, "  Unknown %c Message\n", message_types[x].type);
    if (clientaddr)
      sendto(fd, "UNK\n", 4, flags, (struct sockaddr *) clientaddr, sizeof *clientaddr);
    else
      write(fd, "UNK\n", 4);
    rvalue = 2;
    break;
  }
  return rvalue;
}
// https://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way
// Stores the trimmed input string into the given output buffer, which must be
// large enough to store the result.  If it is too small, the output is
// truncated.
size_t trimwhitespaceX(char *out, size_t len, const char *str)
{
  if(len == 0)
    return 0;

  const char *end;
  size_t out_size;

  // Trim leading space
  while(isspace((unsigned char)*str)) str++;

  if(*str == 0)  // All spaces?
  {
    *out = 0;
    return 1;
  }

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace((unsigned char)*end)) end--;
  end++;

  // Set output size to minimum of trimmed string length and buffer size minus 1
  out_size = (end - str) < len-1 ? (end - str) : len-1;

  // Copy trimmed string and add null terminator
  memcpy(out, str, out_size);
  out[out_size] = 0;

  return out_size;
}


//client connection
void handle_udp_connx(int listenfd) {
  struct sockaddr_in clientaddr;
  socklen_t addrlen = sizeof clientaddr;

  // MSG_WAITALL or 0????
  int len = recvfrom(listenfd, buffer, BSIZE-1, MSG_WAITALL, (struct sockaddr *) &clientaddr, &addrlen);

  // Exprimental: Create the time before the fork and "mark off" if we are the first
  // in this minute. The child process which is the first in the minute immediate injects a
  // clock event.
  unsigned long xnow = time(NULL);
  unsigned long cur_minute = xnow / 10;
  bool new_minute = (cur_minute != epoch_minute);
  epoch_minute = cur_minute;


  if (gDEBUG) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    fprintf(gFOUTPUT, "%d%02d%02d %02d:%02d:%02d ", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
  }

  if (len == -1) {
    if (gDEBUG)
      fprintf(gFOUTPUT, "recvfrom error\n");
    return;
  }
  buffer[len] = '\0';

  char peer[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET, &clientaddr.sin_addr, peer, sizeof peer);

  if (gDEBUG) {
    fprintf(gFOUTPUT, "(%s) ", peer);
    fprintf(gFOUTPUT, "\x1b[32m + [%d]\x1b[0m\n", len);
  }


  char lbuff[ONE_EVENT_BUFFER_SIZE];
  size_t res = trimwhitespaceX(lbuff, ONE_EVENT_BUFFER_SIZE, (const char *) buffer);
  if (gDEBUG) {
    fprintf(gFOUTPUT,"%s\n",lbuff);
    fflush(gFOUTPUT);
  }
  if ((lbuff[0] != '{') || (lbuff[strlen(lbuff) -1] != '}')) {
      if (gDEBUG) {
        fprintf(gFOUTPUT,"INVALID, not processing: [%s]\n",lbuff);
        fflush(gFOUTPUT);
      }
  } else {
    handle_event((uint8_t *)lbuff, listenfd, &clientaddr, peer, new_minute);
  }
}

void handle_tcp_connx(int listenfd) {
  // listen for incoming connections
  if (listen(listenfd, 1000000) != 0 ) {
    perror("listen() error");
    return;
  }

  // Ignore SIGCHLD to avoid zombie threads
  signal(SIGCHLD,SIG_IGN);
  //  signal(SIGCHLD,zombie_hunter);

  // ACCEPT connections
  pid_t ppid = getpid();

  while (1) {
    struct sockaddr_in clientaddr;
    socklen_t addrlen = sizeof clientaddr;
    int clientfd = accept (listenfd, (struct sockaddr *) &clientaddr, &addrlen);

    if (gDEBUG > 2)
      fprintf(gFOUTPUT, "accept (%d)\n", clientfd);

    if (clientfd < 0) {
      if (gDEBUG)
	fprintf(gFOUTPUT, "accept error\n");
    } else {

      if (fork() == 0) { // the child


#if __linux__
	if (prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
	  perror("prctl for child");
	  exit(1);
	}
#endif

#if 0
	if (getppid() != ppid) exit(1);
#endif
	char peer[INET6_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientaddr.sin_addr, peer, sizeof peer);

	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	if (gDEBUG) {
	  fprintf(gFOUTPUT, "%d%02d%02d %02d:%02d:%02d ", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	  fprintf(gFOUTPUT, "(%s) Connected %d\n", peer, clientfd);
	  fflush(gFOUTPUT);
	}

	while(1) {
          fflush(gFOUTPUT);
	  int rcvd;
#ifdef USESELECT
	  fd_set fds;
	  FD_ZERO(&fds);
	  FD_SET(clientfd, &fds);

	  struct timeval tv = {DATA_TIMEOUT, 0};

	  uint8_t a = select(clientfd+1, &fds, NULL, NULL, &tv);
	  if (a < 0) {
	    now = time(NULL);
	    tm = localtime(&now);
	    if (gDEBUG) {
	      fprintf(gFOUTPUT, "%d%02d%02d %02d:%02d:%02d ",
		      tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	      fprintf(gFOUTPUT, "(%s) select error\n", peer);
	      fflush(gFOUTPUT);
	    }
	    break;
	  }
	  if (a == 0) {
	    now = time(NULL);
	    tm = localtime(&now);
	    if (gDEBUG) {
	      fprintf(gFOUTPUT, "%d%02d%02d %02d:%02d:%02d ",
		      tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	      fprintf(gFOUTPUT, "(%s) timeout\n", peer);
	      fflush(gFOUTPUT);
	    }
	    break;
	  }
	  if (FD_ISSET(clientfd, &fds))
	    rcvd = recv(clientfd, buffer, BSIZE, 0);
	  else
	    rcvd = -1;
#else
	  rcvd = recv(clientfd, buffer, BSIZE, 0);
#endif

	  now = time(NULL);
	  tm = localtime(&now);
	  if (gDEBUG) {
	    fprintf(gFOUTPUT, "%d%02d%02d %02d:%02d:%02d ",
		    tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	    fprintf(gFOUTPUT, "(%s) ", peer);
	  }

	  if (gDEBUG > 2)
	    fprintf(gFOUTPUT, "[%d] ", getpid());

	  if (rcvd < 0) {    // receive error
	    if (gDEBUG)
	      fprintf(gFOUTPUT, "  read/recv error\n");
	    break;
	  } else if (rcvd == 0) {    // receive socket closed
	    if (gDEBUG)
	      fprintf(gFOUTPUT, " Client disconnected\n");
	    break;
	  }

	  // message received
	  if (gDEBUG)
	    fprintf(gFOUTPUT, "\x1b[32m + [%d]\x1b[0m\n", rcvd);

          // I probably should do a "new_minute" calculation here,
          // but I don't know how.
	  handle_event(buffer, clientfd, NULL, peer, false);
	}
	//Closing SOCKET
	fflush(gFOUTPUT);
	shutdown(clientfd, SHUT_RDWR);         //All further send and recieve operations are DISABLED...
	close(clientfd);
      }


    }
  }
}
