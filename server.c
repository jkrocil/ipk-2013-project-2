/* IPK 2013/2014
 * Projekt c.2 - Prenos souboru s omezenim rychlosti
 * Autor: Jan Krocil
 *        xkroci02@stud.fit.vutbr.cz
 * Datum: 17.3.2014
 *
 * ----------------------------------
 * Protocol (text,TCP):
 * client->server:
 *   FILENAME: filename.txt              \n
 * server->client:
 *   STATUS: OK, NOT_FOUND, BUSY, ERROR  \n
 *   FILESIZE: file size [bytes])        \n
 *   CONTENT:                            \n
 *   data in binary
 * ----------------------------------
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <semaphore.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define STR_BUFF_SIZE 4096
#define DATA_BUFF_SIZE 32768
#define MAX_CONCURRENT_CONNS 8


// structures
struct shmem_segment {
  sem_t mutex;
  int concurrent_conns;
};


struct parsed_args {
  // server -d limit [kB] -p port
  unsigned long limit; // limit
  int port;            // port
};
// --------


// prototypes
void kill_child_processes();
void wait_for_child_processes();
void reap_child_process();
ssize_t read_line(int fildes, char *buf, ssize_t buff_size);
int64_t get_filesize(FILE *stream);
int bind_socket(int port, int sock);
int serve_file(int sock);
void accept_connections(int sock);
int parse_args(char *argv[], struct parsed_args *p_args);
// --------


// globals
struct shmem_segment *shmem;
int welcome_sock;
int DEBUG = 0;
// --------


void kill_child_processes()
{
  kill(0, SIGKILL);
  wait_for_child_processes();
  close(welcome_sock);
  sem_destroy(&shmem->mutex);
  munmap(&shmem, sizeof(struct shmem_segment));
}


void wait_for_child_processes()
{
  while (1) {
    if (wait(NULL) == -1) {
      if (errno == ECHILD)
        break; // all child processes are over
      else
        kill_child_processes();
    }
  }
}


void reap_child_process()
{
  int keep_waiting = 1;
  while (keep_waiting)
    keep_waiting = (waitpid(-1, NULL, WNOHANG) >= 0);
}


ssize_t read_line(int fildes, char *buf, ssize_t buff_size) {
  ssize_t b_read = 0;
  ssize_t b_read_all = 0;
  while ((b_read = read(fildes, buf + b_read_all, 1)) == 1) {
    b_read_all++;
    if (b_read_all >= buff_size)
      return -1;
    if (buf[b_read_all-1] == '\n')
      break;
  }
  buf[b_read_all] = '\0';
  if (b_read < 0)
    return b_read;
  return b_read_all;
}


int64_t get_filesize(FILE *stream) {
  int64_t filesize = 0;
  fseek(stream, 0L, SEEK_END);
  filesize = ftell(stream);
  return filesize;
}


int bind_socket(int port, int sock) {
  struct sockaddr_in sock_in;
  struct hostent *host_e = NULL;

  sock_in.sin_family = PF_INET;
  sock_in.sin_port = htons(port);
  if ((host_e = gethostbyname("127.0.0.1")) == NULL)
    return 1;
  memcpy(&(sock_in.sin_addr), host_e->h_addr_list[0], host_e->h_length);

  if (bind(sock, (struct sockaddr *)&(sock_in), sizeof(sock_in)) < 0)
    return 1;

  return 0;
}


int send_file(FILE *in_file, int out_sock)  {
  char data_buff[DATA_BUFF_SIZE] = "";
  int64_t bytes_read = 0, bytes_read_all, bytes_written;
  while ((bytes_read = fread(in_sock, data_buff, DATA_BUFF_SIZE)) > 0) {
    bytes_read_all += bytes_read;
    if ((bytes_written = fwrite(data_buff, 1, bytes_read, out_file)) <= 0)
      break;
  }
  if ((bytes_read < 0) || (bytes_written < 0) || (bytes_read_all != filesize))
    return 1;

  return 0;
}

int attend_client(int sock) {
  int status = 0;
  char str_buff[STR_BUFF_SIZE] = "";

  //
  if (read_line(sock, str_buff, STR_BUFF_SIZE) < 0) {
    status = 1;
    goto child_cleanup;
  }
  if (sscanf() { // filename
    status = 1;
    goto child_cleanup;
  }

  // open, NOTFOUND ?


  // read and write
  if (write(sock, str_buff, strlen(str_buff)) < 0)
    return 1;

  child_cleanup:
  sem_wait(&shmem->mutex);
  shmem->concurrent_conns--;
  sem_post(&shmem->mutex);

  return status;
}


void accept_connections(int sock) {
  int pid = 0;
  int data_sock = 0;

  while (1) {
    data_sock = accept(sock, NULL, NULL);
    sem_wait(&shmem->mutex);
    if (shmem->concurrent_conns < MAX_CONCURRENT_CONNS) {
      pid = fork();
      if (pid == -1) { // error
        kill_child_processes();
        exit(1);
      }
      else if (pid == 0) { // child
        close(sock);
        int ret = serve_file(data_sock);
        exit(ret);
      }
      else {
        close(data_sock);
        shmem->concurrent_conns++;
      }
    }
    else
      write(sock, "STATUS: BUSY\n", 13);
    sem_post(&shmem->mutex);
  }

}


int parse_args(char *argv[], struct parsed_args *p_args) {
  int status = 0;
  int limit_i = 0, port_i;

  if ((strcmp("-d", argv[1]) == 0) && (strcmp("-p", argv[3]) == 0)) {
    limit_i = 2;
    port_i = 4;
  }
  else if ((strcmp("-p", argv[1]) == 0) && (strcmp("-d", argv[3]) == 0)) {
    port_i = 2;
    limit_i = 4;
  }
  else
    return 1;

  // port
  status = sscanf(argv[port_i], "%d", &(p_args->port));
  if ((status != 1) || (p_args->port > 65535) || (p_args->port < 0))
    return 1;

  // limit
  status = sscanf(argv[limit_i], "%lu", &(p_args->limit));
  if (status != 1)
    return 1;

  if (DEBUG) {
    printf("Parsed args:\n"
           "Port: %d\n"
           "Limit: %lu\n"
           "\n",
           p_args->port, p_args->limit);
  }

  return 0;
}


// -------   *   MAIN   *   -------
int main(int argc, char *argv[]) {
  int status = 0;
  struct parsed_args p_args = {0,0};

  // parse args
  if (argc == 5 && strcmp(argv[4], "--debug") == 0)
    DEBUG = 1;
  else if (argc != 4) {
    status = 1;
    fprintf(stderr, "Error: Invalid argument.\n");
    printf("%s -p PORT -d LIMIT [--debug]\n"
           "    PORT: number of port to listen on\n"
           "    LIMIT: bandwidth limit to use for each connection [kB]\n"
           "\n", argv[0]);
    goto quit;
  }

  if ((status = parse_args(argv, &p_args)) != 0) {
    fprintf(stderr, "Error: Failed to parse given URL.\n");
    goto quit;
  }
  // --------


  // open socket
  if ((welcome_sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    fprintf(stderr, "Error: Failed to open stream socket.\n");
    goto quit;
  }
  // --------


  // bind socket
  if (bind_socket(p_args.port, welcome_sock) != 0) {
    fprintf(stderr, "Error: Failed to bind socket.\n");
    goto close_socket;
  }
  // --------


  // listen on socket
  if (listen(welcome_sock, 0) != 0) {
    fprintf(stderr, "Error: Failed to mark socket for listening.\n");
    goto close_socket;
  }
  // --------


  // map shared memory and mutex
  shmem = mmap(NULL, sizeof(struct shmem_segment), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  if (shmem == MAP_FAILED) {
    fprintf(stderr, "Error: Failed to map shared memory.\n");
    status = 1;
    goto close_socket;
  }
  shmem->concurrent_conns = 0;
  sem_init(&shmem->mutex, 1, 1);
  // --------


  // register signal handlers
  signal(SIGCHLD, &reap_child_process);
  signal(SIGTERM, &kill_child_processes);
  signal(SIGINT, &kill_child_processes);
  // --------


  // accept connections
  accept_connections(welcome_sock);
  // --------


  // cleanup
  close_socket:
  if (close(welcome_sock) != 0) {
    fprintf(stderr, "Error: Failed to close socket.\n");
    status = 1;
  }
  // --------

  quit:
  return EXIT_FAILURE;
}
// --------------------------------
