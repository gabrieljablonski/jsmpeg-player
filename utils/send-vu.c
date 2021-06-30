#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h> 
#include <inttypes.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

#define MAX_PID 8191
#define DEFAULT_PID 717
#define DEFAULT_TRUNCATE_VU 32

#define PES_HEADER_SIZE 9
#define TS_HEADER_SIZE 4
#define TS_PACKET_SIZE 188

int ts_payload = 0;
uint8_t ts_continuity_counter = 0x0;
uint8_t ts_packet[TS_PACKET_SIZE];

void send_packet(int pid, char *host, int port, char *path) {
  ts_packet[3] = ts_continuity_counter | 0x30; /* continuity counter, no scrambling, adaptation field and payload */
  ts_continuity_counter = (ts_continuity_counter + 1) % 0x10; /* inc. continuity counter */
  for (int i = 0; i < ts_payload; ++i) { /* move the payload at the end */
    ts_packet[TS_PACKET_SIZE - 1 - i] = ts_packet[TS_HEADER_SIZE + ts_payload - 1 - i];
  }
  ts_packet[4] = TS_PACKET_SIZE - ts_payload - TS_HEADER_SIZE - 1; /* point to the first payload byte */
  ts_packet[5] = 0x00; /* no options */
  for (int i = TS_HEADER_SIZE + 2; i < TS_PACKET_SIZE - ts_payload; ++i) { /* pad the packet */
    ts_packet[i] = 0xFF;
  }

  char *http_header = "POST %s HTTP/1.1\r\nTransfer-Encoding: chunked\r\nUser-Agent: xcoder\r\nAccept: /\r\nConnection: close\r\nHost: localhost\r\nIcy-MetaData: 1\r\n\r\n";
  char message[10240];

  snprintf(message, 10249, http_header, path);
  int base_len = strlen(message);

  // 188 bytes
  message[base_len] = 0x62;
  message[base_len + 1] = 0x63;
  message[base_len + 2] = '\r';
  message[base_len + 3] = '\n';
  memcpy(message + base_len + 4, ts_packet, TS_PACKET_SIZE);

  // indicate message ended ('transfer-encoding: chunked' requirement)
  message[base_len + TS_PACKET_SIZE + 4] = '\r';
  message[base_len + TS_PACKET_SIZE + 5] = '\n';
  message[base_len + TS_PACKET_SIZE + 6] = 0x30;
  message[base_len + TS_PACKET_SIZE + 7] = '\r';
  message[base_len + TS_PACKET_SIZE + 8] = '\n';
  message[base_len + TS_PACKET_SIZE + 9] = '\r';
  message[base_len + TS_PACKET_SIZE + 10] = '\n';

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);

  if (sockfd < 0) {
    fprintf(stderr, "failed to create socket %d\n", sockfd);
    exit(-1);
  }
  
  struct hostent *server = gethostbyname(host);
  if (!server) {
    fprintf(stderr, "host not found\n");
    exit(-1);
  }

  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

  if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) {
    fprintf(stderr, "failed to connect to server\n");
    exit(-1);
  }

  int total = base_len + TS_PACKET_SIZE + 11;
  int sent = 0;
  do {
    int bytes = write(sockfd, message + sent, total - sent);
    if (bytes < 0) {
      fprintf(stderr, "http request failed\n");
      exit(-1);
    }
    if (bytes == 0) {
      break;
    }
    sent += bytes;
  } while (sent < total);

  char response[10240];
  memset(response, 0, sizeof(response));
  total = sizeof(response) - 1;
  int received = 0;
  do {
    int bytes = read(sockfd, response + received, total - received);
    if (bytes < 0) {
      fprintf(stderr, "http parse response failed\n");
      exit(-1);
    }
    if (bytes == 0) {
      break;
    }
    received += bytes;
  } while (received < total);

  close(sockfd);
  ts_payload = 0;
}

int main(int argc, char **argv)
{
	FILE* vu_file;
  char *host;
  int port;
  char *path;

  int pid = DEFAULT_PID;
  int truncate_vu_file = DEFAULT_TRUNCATE_VU;
	
  if (argc < 5) {
    fprintf(
      stderr, 
      "Usage:\n\n\t%s <vu-file> <host> <port> <path> [<PID> [<truncate-vu>]] (PID defaults to %d. VU file is truncated at %d bytes by default.)\n\n", 
      argv[0], 
      DEFAULT_PID, 
      DEFAULT_TRUNCATE_VU
    );
    return -1;
  }

  vu_file = fopen(argv[1], "rb");
  if (!vu_file) {
    fprintf(stderr, "Failed to open vu file at '%s'.\n", argv[1]);
    return -2;
  }

  host = argv[2];
  port = atoi(argv[3]);
  path = argv[4];

  if (argc > 5) {
		pid = atoi(argv[5]);
  }

  if (argc > 6) {
    truncate_vu_file = atoi(argv[6]);
  }
	
	if (pid < 1 || pid > MAX_PID ) { 
		fprintf(stderr, "Invalid PID %s. Should be in range [1, %d].\n", argv[5], MAX_PID);
    fclose(vu_file);
		return -3;
	}

  while (1) {
	  ts_packet[0] = 0x47; /* sync byte */ 

    // 2 bytes for PID
    ts_packet[1] = 0x40 | pid >> 8;
    ts_packet[2] = 0xff & pid;

    // unused TS header field
    // ts_packet[3]

    // PES header
    ts_packet[4] = 0x00;
    ts_packet[5] = 0x00;
    ts_packet[6] = 0x01;
    // PES stream type
    ts_packet[7] = 0x06;
    // 2 bytes for packet size
    ts_packet[8] = 0x00;
    ts_packet[9] = truncate_vu_file + 3;
    // unused PES header fields
    ts_packet[10] = 0x00;
    ts_packet[11] = 0x00;
    ts_packet[12] = 0x00;

    int bytes_read = fread(ts_packet + TS_HEADER_SIZE + PES_HEADER_SIZE, 1, truncate_vu_file, vu_file);
    if (bytes_read != truncate_vu_file) {
      fprintf(stderr, "Failed to read VU file.\n");
      fclose(vu_file);
      return -4;
    }
    ts_payload = bytes_read + PES_HEADER_SIZE;
    send_packet(pid, host, port, path);

    fclose(vu_file);
    vu_file = fopen(argv[1], "rb");
    if (!vu_file) {
      fprintf(stderr, "Failed to open vu file at '%s'.\n", argv[1]);
      return -2;
    }
    usleep(30000);
  }

	return 0;
}
