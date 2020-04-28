/************************************
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
***************************************/ 

#define _GNU_SOURCE
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
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/prctl.h> // prctl(), PR_SET_PDEATHSIG
#include <sys/mman.h>
#include "main.h"

#define UDP 0
#define TCP 1

#define DEBUG
#ifdef DEBUG
int gDEBUG = 0;
#endif

//#define USESELECT

#define CONNMAX 1000

struct clients_t {
  int fd;
  uint8_t ip[20];
  uint8_t id[32];
} *clients;

struct packet_t {
  uint8_t M;
  uint8_t type;
  uint8_t loc;
  uint8_t num;
  uint32_t ms;
  int32_t data;
};

#define BSIZE 65*1024
uint8_t buffer[BSIZE];

int main(int argc, char* argv[]) {
  int mode = UDP;
  int opt;
  while ((opt = getopt(argc, argv, "Dt")) != -1) {
    switch (opt) {
    case 'D': gDEBUG++; break;
    case 't': mode = TCP; break;
    default: fprintf(stderr, "Usage: %s [-D] [-t] [port]", argv[0]);
      exit(1);
    }
  }

  char *port = "6111";
  if (mode == TCP)
    port = "6110";

  if (optind < argc) {
    port = argv[optind];
  }

  printf("%s Server started %son port %s%s\n",
	  mode == TCP ? "TCP":"UDP",
	  "\033[92m", port, "\033[0m"
	  );

  if (mode == TCP) { // set up shared memory for children
    int protection = PROT_READ | PROT_WRITE;
    // other child processes can access it only
    int visibility = MAP_SHARED | MAP_ANONYMOUS;

    void *shmem = mmap(NULL, CONNMAX * sizeof(struct clients_t), protection, visibility, -1, 0);
    //  memcpy(shmem, "0", 1);
    clients = shmem;

    // Setting all elements to -1: signifies there is no client connected
    for (int i = 0; i < CONNMAX; i++) {
      clients[i].fd = -1;
      clients[i].id[0] = '\0';
    }
  }
  
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
    //    munmap(clients, CONNMAX * sizeof(struct clients_t));
    exit(1);
  }

  // socket and bind
  struct addrinfo *p;
  int listenfd;
  for (p = res; p != NULL; p = p->ai_next) {
    listenfd = socket (p->ai_family, p->ai_socktype, 0);
    if (listenfd == -1) continue;
    int option = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0) break;
  }

  freeaddrinfo(res);

  if (p == NULL) {
    perror ("socket() or bind()");
    //    munmap(clients, CONNMAX * sizeof(struct clients_t));
    exit(1);
  }

  while (1) {
    if (mode == TCP)
      handle_tcp_connx(listenfd);
    else
      handle_udp_connx(listenfd);
  }
}

void
send_params(char *peer, char *addr) {
  printf("HTTP/1.1 200 OK\n\n");
  printf("PARAM_WAIT: 300\n");
  fflush(stdout);
}

void
log_bytecode(char *peer, void *buff) {
  struct __attribute__((__packed__)) packet_t *packet = (struct packet_t *) buff;

  // xxx need file locking
  char fname[30];
  strcpy(fname, "0Logfile.");
  strcpy(fname+9, peer);
  
  FILE *fp = fopen(fname, "a");
  if (!fp) return;

  fprintf(fp, "%lu:%c:%c:%u:%u:%d\n", time(NULL), packet->type, packet->loc, packet->num, ntohl(packet->ms), ntohl(packet->data));
  fclose(fp);
  
  //  printf("OK\r\n");
  //  fflush(stdout);
}

void
log_json(char *peer, void *buff) {
  // xxx need file locking
  char fname[30];
  strcpy(fname, "0Logfile.");
  strcpy(fname+9, peer);
  
  FILE *fp = fopen(fname, "a");
  if (!fp) return;

  char *ptr = strchr((char *)buff, '\n');
  if (ptr) *ptr = '\0';
  if (ptr = strchr((char*)buff, '\r'))
    *ptr = '\0';
      
  fprintf(fp, "%lu:%s\n", time(NULL), (char *)buff);
  fclose(fp);
  //  printf("OK\n");
  //  fflush(stdout);
}

void
print_bytecode_data(void *buff) {
  struct __attribute__((__packed__)) packet_t *packet = (struct packet_t *) buff;
  if (packet->M != 'M') {
    fprintf(stderr, "very strange error - aborting\n");
    return;
  }
    
  switch (packet->type) {
    uint32_t i;
  case 'T': printf("  Temp %c%c (%u): %f C\n", packet->loc, packet->num, ntohl(packet->ms), (float)(int)ntohl(packet->data)/100.0); break;
  case 'P': printf("  Pressure %c%c (%u): %f cm\n", packet->loc, packet->num, ntohl(packet->ms), (float)(int)ntohl(packet->data)/100.0); break;
  case 'D': printf("  Differential Pressure %c%c (%u): %f cm\n", packet->loc, packet->num, ntohl(packet->ms), (float)(int)ntohl(packet->data)/100.0); break;
  case 'F': printf("  Flow %c%c (%u): %f l\n", packet->loc, packet->num, ntohl(packet->ms), (float)(int)ntohl(packet->data)/100.0); break;
  case 'O': printf("  Fractional O2 %c%c (%u): %f%%\n", packet->loc, packet->num, ntohl(packet->ms), (float)(int)ntohl(packet->data)/100.0); break;
  case 'H': printf("  Humidity %c%c (%u): %f%%\n", packet->loc, packet->num, ntohl(packet->ms), (float)(int)ntohl(packet->data)/100.0); break;
  case 'V': printf("  Volume %c%c (%u): %d ml\n", packet->loc, packet->num, ntohl(packet->ms), (int)ntohl(packet->data)); break;
  case 'B': printf("  Breaths %c%c (%u): %d\n", packet->loc, packet->num, ntohl(packet->ms), (int)ntohl(packet->data)/10); break;
  case 'G': printf("  Gas %c%c (%u): %d\n", packet->loc, packet->num, ntohl(packet->ms), (int)ntohl(packet->data)); break;
  case 'A': printf("  Altitude %c%c (%u): %d m\n", packet->loc, packet->num, ntohl(packet->ms), (int)ntohl(packet->data)); break;
  default: printf("Invalid packet type\n");
  }
}

void
print_json_data(void *buff) {
  if (((char *)buff)[0] != '{') {
    fprintf(stderr, "very strange error - aborting\n");
    return;
  }
    
  char *ptr = strchr(buff, '\n');
  if (ptr) *ptr = '\0';
  if (ptr = strchr(buff, '\r'))
    *ptr = '\0';
  printf("%s\n", (char *)buff);
}

#if 0
void zombie_hunter(int sig)
{
  int status;
  waitpid(pid, &status, 0);
  printf("Got status %d from child\n",status);
  finished=1;
}
#endif

//client connection
void handle_udp_connx(int listenfd) {
  struct sockaddr_in clientaddr;
  socklen_t addrlen = sizeof clientaddr;

  // MSG_WAITALL or 0????
  int len = recvfrom(listenfd, buffer, BSIZE-1, MSG_WAITALL, &clientaddr, &addrlen);

  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  printf("%d%02d%02d %02d:%02d:%02d ", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

#ifdef DEBUG
  if (gDEBUG)
    printf("[%d] ", getpid());
#endif
  
  if (len == -1) {
    printf("recvfrom error\n");
    return;
  }
  buffer[len] = '\0';
  
  char peer[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET, &clientaddr.sin_addr, peer, sizeof peer);

  printf("(%s) ", peer);
  // message received
  printf("\x1b[32m + [%d]\x1b[0m\n", len);

  if (buffer[0] == 'M') {
    print_bytecode_data(buffer);
    log_bytecode(peer, buffer);
  } else if (buffer[0] == '{') {
    print_json_data(buffer);
    log_json(peer, buffer);
  } else if (strncmp(buffer, "GET /", 5) == 0) {
    printf("  Param request\n");
    send_params(peer, buffer+5);
  } else {
    printf("  BAD PACKET FORMAT\n");
    // “Be liberal in what you accept, and conservative in what you send” - Jon Postel
    //break;
  }
  sendto(listenfd, "OK", 2, MSG_CONFIRM, (const struct sockaddr *) &clientaddr, addrlen);
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
  int slot = 0;
  pid_t ppid = getpid();
    
  while (1) {
    struct sockaddr_in clientaddr;
    socklen_t addrlen = sizeof clientaddr;
    clients[slot].fd = accept (listenfd, (struct sockaddr *) &clientaddr, &addrlen);
#ifdef DEBUG
    if (gDEBUG)
      fprintf(stderr, "accept on slot %d (%d)\n", slot, clients[slot].fd);
#endif
    if (clients[slot].fd < 0) {
#ifdef DEBUG
      if (gDEBUG)
	perror("accept() error");
#endif      
    } else {
      if (fork() == 0) { // the child
	if (prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
	  perror(0);
	  exit(1);
	}
#if 0
	if (getppid() != ppid) exit(1);
#endif
	char peer[INET6_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientaddr.sin_addr, peer, sizeof peer);
	strncpy(clients[slot].ip, peer, sizeof clients[slot].ip);

	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	printf("%d%02d%02d %02d:%02d:%02d ", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	printf("(%s) Connected\n", peer);
	fflush(stdout);

	int clientfd = clients[slot].fd;

	while(1) {
	  int rcvd;
#ifdef USESELECT
	  fd_set fds;
	  FD_ZERO(&fds);
	  FD_SET(clientfd, &fds);
	  
	  struct timeval tv = {60, 0}; // 60 seconds                                                                                                              
	  uint8_t a = select(clientfd+1, &fds, NULL, NULL, &tv);
	  if (a < 0) {
	    now = time(NULL);
	    tm = localtime(&now);
	    printf("%d%02d%02d %02d:%02d:%02d ", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	    printf("(%s) select error", peer);
	    break;
	  }
	  if (a == 0) {
	    now = time(NULL);
	    tm = localtime(&now);
	    printf("%d%02d%02d %02d:%02d:%02d ", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	    printf("(%s) timeout", peer);
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
	  printf("%d%02d%02d %02d:%02d:%02d ", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	  printf("(%s) ", peer);
#ifdef DEBUG
	  if (gDEBUG)
	    printf("[%d] ", getpid());
#endif
	  
	  if (rcvd < 0) {    // receive error
	    printf("  read/recv error\n");
	    break;
	  } else if (rcvd == 0) {    // receive socket closed
	    printf(" Client disconnected\n");
	    break;
	  }

	  // message received     
	  printf("\x1b[32m + [%d]\x1b[0m\n", rcvd);

	  if (buffer[0] == 'M') {
	    print_bytecode_data(buffer);
	    log_bytecode(peer, buffer);
	  } else if (buffer[0] == '{') {
	    print_json_data(buffer);
	    log_json(peer, buffer);
	  } else if (strncmp(buffer, "GET /", 5) == 0) {
	    printf("  Param request\n");
	    send_params(peer, buffer+5);
	  } else {
	    printf(" BAD PACKET FORMAT\n");
	    // “Be liberal in what you accept, and conservative in what you send” - Jon Postel
	    //break;
	  }
	}
	//Closing SOCKET
	shutdown(clients[slot].fd, SHUT_RDWR);         //All further send and recieve operations are DISABLED...
	close(clients[slot].fd);
	clients[slot].fd = -1; // reset slot open
      }
      int count = 0;
      for(int a=0; a < CONNMAX; a++)
	if(clients[a].fd != -1) count++;
#ifdef DEBUG
      if (gDEBUG)
	fprintf(stderr, "slots in use %d\n", count);
#endif      
      while (clients[slot].fd != -1) {
	slot = (slot+1) % CONNMAX;
      }
    }
  }
}
