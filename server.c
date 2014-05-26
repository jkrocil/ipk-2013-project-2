/* IPK 2013/2014
 * Projekt c.2 - Prenos souboru s omezenim rychlosti
 *               Bandwidth-limited file transmission
 * Autor: Jan Krocil
 *        xkroci02@stud.fit.vutbr.cz
 * Datum: 17.3.2014
 *
 * ----------------------------------
 * Protocol (text,TCP):
 * client --> server:
 *   FILENAME: filename.txt           \n
 * client <-- server:
 *   STATUS: OK|NOT_FOUND|BUSY|ERROR  \n
 *   (if STATUS is OK)
 *   FILESIZE: file size [bytes]      \n
 *   CONTENT:                         \n
 *   data in binary
 * ----------------------------------
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MIN(x, y) ( ( (x) < (y) ) ? (x) : (y) )

#define STR_BUFF_SIZE 4096
#define DATA_BUFF_SIZE 65536
#define MAX_CONCURRENT_CONNS 32


// structures
struct shmem_segment {
  sem_t stdout_mutex;
};


struct parsed_args {
  // server -d limit [kB] -p port
  size_t limit; // limit
  int port;     // port
};
// --------


// prototypes
void term_child_processes_and_exit();
void wait_for_child_processes();
void reap_child_process();
void child_exit();
ssize_t read_line(int fildes, char *buf, ssize_t buff_size);
int64_t get_filesize(FILE *stream);
int bind_socket(int port, int sock);
int open_file_for_reading(char *filename, FILE **file);
size_t send_segment(FILE *in_file, int out_sock, size_t bytes);
int64_t send_file(FILE *in_file, int out_sock, int64_t filesize, size_t limit);
int attend_client(int sock, size_t limit);
void accept_connections(int sock, size_t limit);
int parse_args(char *argv[], struct parsed_args *p_args);
// --------


// globals
struct shmem_segment *shmem;
int concurrent_conns = 0;
int welcome_sock;
int data_sock;
int DEBUG = 0;
pid_t pid = 0;
// --------


void term_child_processes_and_exit() {
  kill(0, SIGTERM);
  wait_for_child_processes();
  close(welcome_sock);
  sem_destroy(&shmem->stdout_mutex);
  munmap(&shmem, sizeof(struct shmem_segment));
  printf("Exiting...\n");
  exit(EXIT_FAILURE);
}


void wait_for_child_processes() {
  while (1) {
    if ((wait(NULL) == -1) && (errno == ECHILD))
        break; // all child processes are over
  }
}


void reap_child_process() {
    waitpid(-1, NULL, WNOHANG);
}


void child_exit() {
  close(data_sock);
  exit(EXIT_FAILURE);
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
  long original_pos = ftell(stream);
  fseek(stream, 0L, SEEK_END);
  filesize = ftell(stream);
  fseek(stream, original_pos, SEEK_SET);
  return filesize;
}


int bind_socket(int port, int sock) {
  struct sockaddr_in sock_in = {0};
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


int open_file_for_reading(char *filename, FILE **file) {
  if (access(filename, R_OK) != 0) {
    if (errno == ENOENT)
      return 2;
    return 1;
  }

  *file = fopen(filename, "r");
  if (*file == NULL)
    return 1;

  return 0;
}


size_t send_segment(FILE *in_file, int out_sock, size_t bytes) {
  char data_buff[DATA_BUFF_SIZE] = "";
  size_t bytes_read = 0, bytes_sent_total = 0;
  ssize_t bytes_sent = 0;
  size_t bytes_to_send = MIN(bytes, DATA_BUFF_SIZE);

  while (bytes_to_send > 0) {
    bytes_read = fread(data_buff, 1, bytes_to_send, in_file);
    if ((bytes_read == 0) && (!feof(in_file)))
      return -1;
    bytes_sent = write(out_sock, data_buff, bytes_read);
    if (bytes_sent < 0)
      return -1;
    bytes_sent_total += bytes_sent;
    bytes_to_send = MIN((bytes - bytes_sent_total), DATA_BUFF_SIZE);
  }

  return bytes_sent_total;
}


int64_t send_file(FILE *in_file, int out_sock, int64_t filesize, size_t limit) {
  int64_t bytes_to_send_sec = 0, bytes_sent_total = 0;
  size_t bytes_sent_sec = 0;
  time_t current_sec = 0;

  do {
    if (current_sec == time(NULL))
      usleep(2000);
    else {
      current_sec = time(NULL);
      bytes_to_send_sec = MIN((filesize - bytes_sent_total), limit);
      bytes_sent_sec = send_segment(in_file, out_sock, bytes_to_send_sec);
      if (bytes_sent_sec <= 0)
        break;
      if (DEBUG) {
        sem_wait(&shmem->stdout_mutex);
        printf("[%d] %ld bytes sent in time %ld\n", pid, bytes_sent_sec, current_sec);
        sem_post(&shmem->stdout_mutex);
      }
      bytes_sent_total += bytes_sent_sec;
    }
  } while (1);

  if (DEBUG) {
    sem_wait(&shmem->stdout_mutex);
    printf("[%d] %" PRId64 " bytes sent\n", pid, bytes_sent_total);
    sem_post(&shmem->stdout_mutex);
  }
  return bytes_sent_total;
}


int attend_client(int sock, size_t limit) {
  int status = 0;
  char str_buff[STR_BUFF_SIZE] = "";
  char filename[256] = "";
  FILE *file = NULL;
  int64_t filesize = 0;

  // load request
  if (DEBUG) {
    sem_wait(&shmem->stdout_mutex);
    printf("[%d] Loading client file request...\n", pid);
    sem_post(&shmem->stdout_mutex);
  }
  if (read_line(sock, str_buff, STR_BUFF_SIZE) < 0) {
    status = 1;
    write(sock, "STATUS: ERROR\n", 14);
    goto child_close_socket;
  }

  // get filename
  if (DEBUG) {
    sem_wait(&shmem->stdout_mutex);
    printf("[%d] Getting filename...\n", pid);
    sem_post(&shmem->stdout_mutex);
  }
  if (sscanf(str_buff, "FILENAME: %255s\n", filename) != 1) {
    status = 1;
    write(sock, "STATUS: ERROR\n", 14);
    goto child_close_socket;
  }

  // open file
  if (DEBUG) {
    sem_wait(&shmem->stdout_mutex);
    printf("[%d] Opening requested file '%s'...\n", pid, filename);
    sem_post(&shmem->stdout_mutex);
  }
  status = open_file_for_reading(filename, &file);
  if (status != 0) {
    if (status == 2)
      write(sock, "STATUS: NOT_FOUND\n", 18);
    else
      write(sock, "STATUS: ERROR\n", 14);
    goto child_close_socket;
  }

  // get filesize
  filesize = get_filesize(file);

  // send headers
  if (DEBUG) {
    sem_wait(&shmem->stdout_mutex);
    printf("[%d] Sending headers...\n", pid);
    sem_post(&shmem->stdout_mutex);
  }
  write(sock, "STATUS: OK\n", 11);
  sprintf(str_buff, "FILESIZE: %" PRId64 "\n", filesize);
  write(sock, str_buff, strlen(str_buff));
  write(sock, "CONTENT:\n", 9);

  // send file
  if (DEBUG) {
    sem_wait(&shmem->stdout_mutex);
    printf("[%d] Sending file...\n", pid);
    sem_post(&shmem->stdout_mutex);
  }
  if (send_file(file, sock, filesize, limit) != filesize)
    status = 1;

  // cleanup
  fclose(file);

  child_close_socket:
  close(sock);

  return status;
}


void accept_connections(int sock, size_t limit) {
  int fork_ret = 0;

  while (1) {
    data_sock = accept(sock, NULL, NULL);
    if (DEBUG) {
      sem_wait(&shmem->stdout_mutex);
      printf("Accepted new connection...\n");
      sem_post(&shmem->stdout_mutex);
    }
    if (concurrent_conns < MAX_CONCURRENT_CONNS) {
      fork_ret = fork();

      if (fork_ret == -1) { // error
        close(data_sock);
        term_child_processes_and_exit();
      }

      else if (fork_ret == 0) { // child
        pid = getpid();
        close(sock);
        signal(SIGTERM, &child_exit);
        int ret = attend_client(data_sock, limit);
        if (DEBUG) {
          sem_wait(&shmem->stdout_mutex);
          if (ret == 0)
            printf("[%d] File sent.\n", pid);
          else if (ret == 1)
            printf("[%d] Failed to send file.\n", pid);
          else
            printf("[%d] File not found.\n", pid);
          sem_post(&shmem->stdout_mutex);
        }
        exit(ret);
      }

      else { // parent
        close(data_sock);
        concurrent_conns++;
      }

    }
    else {
      if (DEBUG) {
        sem_wait(&shmem->stdout_mutex);
        printf("Refused - too busy.\n");
        sem_post(&shmem->stdout_mutex);
      }
      write(data_sock, "STATUS: BUSY\n", 13);
      close(data_sock);
    }
  }

}


int parse_args(char *argv[], struct parsed_args *p_args) {
  int status = 0;
  int limit_i = 0, port_i = 0;

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
           "Limit: %lu [kB]\n"
           "\n",
           p_args->port, p_args->limit);
  }

  p_args->limit = 1000 * p_args->limit;

  return 0;
}


// -------   *   MAIN   *   -------
int main(int argc, char *argv[]) {
  int status = 0;
  struct parsed_args p_args = {0,0};

  // parse args
  if (argc == 6 && strcmp(argv[5], "--debug") == 0)
    DEBUG = 1;
  else if (argc != 5) {
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


  // open, bind and mark welcoming socket for listening
  if (DEBUG) printf("Opening socket...\n");
  if ((welcome_sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    fprintf(stderr, "Error: Failed to open stream socket.\n");
    goto quit;
  }

  if (DEBUG) printf("Binding socket...\n");
  if (bind_socket(p_args.port, welcome_sock) != 0) {
    fprintf(stderr, "Error: Failed to bind socket.\n");
    goto close_socket;
  }

  if (DEBUG) printf("Marking socket for listening...\n");
  if (listen(welcome_sock, 0) != 0) {
    fprintf(stderr, "Error: Failed to mark socket for listening.\n");
    goto close_socket;
  }
  // --------


  // map and init shared memory
  if (DEBUG) printf("Mapping shared memory...\n");
  shmem = mmap(NULL, sizeof(struct shmem_segment), PROT_READ|PROT_WRITE,
               MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  if (shmem == MAP_FAILED) {
    fprintf(stderr, "Error: Failed to map shared memory.\n");
    status = 1;
    goto close_socket;
  }
  sem_init(&shmem->stdout_mutex, 1, 1);
  // --------


  // register signal handlers
  if (DEBUG) printf("Registering signal handlers...\n");
  signal(SIGCHLD, &reap_child_process);
  signal(SIGTERM, &term_child_processes_and_exit);
  signal(SIGINT, &term_child_processes_and_exit);
  // --------


  // accept connections
  printf("Accepting connections on port %d\n", p_args.port);
  accept_connections(welcome_sock, p_args.limit);
  // --------


  // cleanup
  close_socket:
  close(welcome_sock);
  // --------

  quit:
  return EXIT_FAILURE;
}
// --------------------------------
