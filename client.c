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
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#define MIN(x, y) ( ( (x) < (y) ) ? (x) : (y) )

#define STR_BUFF_SIZE 4096
#define DATA_BUFF_SIZE 65536


// structures
struct parsed_url {
  // host:port/filename
  char hostname[256]; // host
  int  port;          // port
  char filename[256]; // filename
};
// --------


// prototypes
int parse_url(char *raw_url, struct parsed_url *p_url);
ssize_t read_line(int fildes, char *buf, ssize_t buff_size);
int connect_to_server(char *hostname, int port, int sock);
int exchange_info(char *filename, int64_t *filesize, int sock);
int download_file(int in_sock, FILE *out_file, int64_t filesize);
// --------


// globals
int DEBUG = 0;
// --------


int parse_url(char *raw_url, struct parsed_url *p_url) {
  char str_buff[STR_BUFF_SIZE] = "";
  char *c = raw_url, *str_i = NULL;

  if ((strlen(raw_url) == 0) || (strlen(raw_url) >= STR_BUFF_SIZE))
    return 1;

  // hostname
  str_i = str_buff;
  while(*c && (*c != ':')) {
    *str_i++ = *c++;
  }
  *str_i = '\0';
  if (strlen(str_buff) == 0 || (strlen(str_buff) >= sizeof(p_url->hostname)))
    return 1;
  strcpy(p_url->hostname, str_buff);

  // skip ':'
  c++;

  // port
  str_i = str_buff;
  while(*c && (*c != '/')) {
    if (!isdigit(*c))
      return 1;
    *str_i++ = *c++;
  }
  *str_i = '\0';
  if ((strlen(str_buff) == 0) || (strlen(str_buff) > 5))
    return 1;
  p_url->port = atoi(str_buff);
  if (p_url->port > 65535)
    return 1;

  // skip '/'
  if (*c == '/')
    c++;
  else
    return 1;

  // filename
  str_i = str_buff;
  while(*c)
    *str_i++ = *c++;
  *str_i = '\0';
  if (strlen(str_buff) == 0 || (strlen(str_buff) >= sizeof(p_url->filename)))
    return 1;
  strcpy(p_url->filename, str_buff);

  if (DEBUG) {
    printf("Parsed URL:\n"
           "Host: '%s'\n"
           "Port: '%d'\n"
           "Filename: '%s'\n"
           "\n",
           p_url->hostname, p_url->port, p_url->filename);
  }

  return 0;
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


int connect_to_server(char *hostname, int port, int sock) {
  struct sockaddr_in sock_in = {0};
  struct hostent *host_e = NULL;

  sock_in.sin_family = PF_INET;
  sock_in.sin_port = htons(port);
  if ((host_e = gethostbyname(hostname)) == NULL)
    return 1;
  memcpy(&(sock_in.sin_addr), host_e->h_addr_list[0], host_e->h_length);

  if (connect(sock, (struct sockaddr *)&(sock_in), sizeof(sock_in)) < 0)
    return 1;

  return 0;
}


int exchange_info(char *filename, int64_t *filesize, int sock) {
  char str_buff[STR_BUFF_SIZE] = "";
  sprintf(str_buff, "FILENAME: %s\n", filename);
  if (write(sock, str_buff, strlen(str_buff)) < 0)
    return 1;
  if (read_line(sock, str_buff, STR_BUFF_SIZE) < 0)
    return 1;
  if (DEBUG) printf("%s", str_buff);

  if (strcmp(str_buff, "STATUS: NOT_FOUND\n") == 0)
    return -1;
  else if (strcmp(str_buff, "STATUS: BUSY\n") == 0)
    return -2;
  else if (strcmp(str_buff, "STATUS: ERROR\n") == 0)
    return -3;
  else if (strcmp(str_buff, "STATUS: OK\n") != 0)
    return 1;

  if (read_line(sock, str_buff, STR_BUFF_SIZE) < 0)
    return 1;
  if (DEBUG) printf("%s", str_buff);
  if (sscanf(str_buff, "FILESIZE: %" SCNd64 "\n", filesize) != 1)
    return 1;

  if (read_line(sock, str_buff, STR_BUFF_SIZE) < 0)
    return 1;
  else if (strcmp(str_buff, "CONTENT:\n") != 0)
    return 1;

  return 0;
}


int download_file(int in_sock, FILE *out_file, int64_t filesize) {
  char data_buff[DATA_BUFF_SIZE] = "";
  int64_t bytes_read = 0, bytes_read_all = 0, bytes_written = 0;
  int64_t bytes_to_read = MIN(filesize, DATA_BUFF_SIZE);

  while (bytes_to_read > 0) {
      if ((bytes_read = read(in_sock, data_buff, bytes_to_read)) <= 0)
        break;
      bytes_read_all += bytes_read;
      bytes_to_read = MIN((filesize - bytes_read_all), DATA_BUFF_SIZE);
      if ((bytes_written = fwrite(data_buff, 1, bytes_read, out_file)) <= 0)
        break;
  }
  if ((bytes_read < 0) || (bytes_written < 0) || (bytes_read_all != filesize))
    return 1;

  return 0;
}


// -------   *   MAIN   *   -------
int main(int argc, char *argv[]) {
  int status = 0;
  struct parsed_url p_url = {{0}};
  int sock = 0;
  int64_t filesize = 0;
  FILE *out_file = NULL;

  // parse args
  if (argc == 3 && strcmp(argv[2], "--debug") == 0)
    DEBUG = 1;
  else if (argc != 2) {
    status = 1;
    fprintf(stderr, "Error: Invalid argument.\n");
    printf("%s URL [--debug]\n    URL: host:port/filename\n\n", argv[0]);
    goto quit;
  }

  if ((status = parse_url(argv[1], &p_url)) != 0) {
    fprintf(stderr, "Error: Failed to parse given URL.\n");
    goto quit;
  }
  // --------


  // open socket
  if (DEBUG) printf("Opening socket...\n");
  if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    fprintf(stderr, "Error: Failed to open stream socket.\n");
    goto quit;
  }
  // --------


  // connect to server
  if (DEBUG) printf("Connecting to server...\n");
  status = connect_to_server(p_url.hostname, p_url.port, sock);
  if (status != 0) {
    fprintf(stderr, "Error: Failed to connect to server.\n");
    goto close_socket;
  }

  // send file request; get status and filesize
  if (DEBUG) printf("Exchanging initial information with server...\n");
  status = exchange_info(p_url.filename, &filesize, sock);
  if (status != 0) {
    if (status == -1)
      fprintf(stderr, "Error: File '%s' not found on server.\n", p_url.filename);
    else if (status == -2)
      fprintf(stderr, "Error: Server is busy, try again later.\n");
    else if (status == -3)
      fprintf(stderr, "Error: Failed to open file '%s' on server.\n", p_url.filename);
    else
      fprintf(stderr, "Error: Failed during initial data exchange with server.\n");
    goto close_socket;
  }
  // --------


  // open file
  if (DEBUG) printf("Opening empty file...\n");
  out_file = fopen(p_url.filename, "w");
  if (out_file == NULL) {
    fprintf(stderr, "Error: Failed to create file '%s' in current working directory.\n", p_url.filename);
    goto close_socket;
  }
  // --------


  // download data to file
  printf("Downloading file...\n");
  status = download_file(sock, out_file, filesize);
  if (status != 0) {
    fprintf(stderr, "Error: Failed to download file.\n");
  }
  else
    printf("Download complete.\n");
  // --------


  // cleanup
  fclose(out_file);
  close_socket:
  close(sock);
  // --------


  quit:
  if (status != 0)
    return EXIT_FAILURE;

  return EXIT_SUCCESS;
}
// --------------------------------
