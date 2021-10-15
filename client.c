#include <netdb.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>

#define RECV_BUFF_LEN 256

int main(int argc, char* const argv[]) {

  int s;
  struct sockaddr_in addr;
  struct termios raw, origin;

  if(argc < 3) {
    printf("Usage: %s <ip_addr> <port>\n", argv[0]);
    return 1;
  }

  if((s = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket ");
    return 1;
  }

  addr.sin_family = AF_INET;
  if(!inet_aton(argv[1], &(addr.sin_addr))) {
    while(1) {

      struct hostent* host = gethostbyname(argv[1]);
      if(!host) {
        switch(h_errno) {
          case HOST_NOT_FOUND:
            fprintf(stderr, "Unknown host \"%s\"\n", argv[1]);
            return 1;
          case NO_RECOVERY:
            fprintf(stderr, "Domain name resolution server failure\n");
            return 1;
          case NO_DATA:
            fprintf(stderr, "No AF_INET address associated to host %s\n", argv[1]);
            return 1;
          case TRY_AGAIN:
            fprintf(stderr, "Can not resolve domain name, trying again in 5 seconds\n");
            sleep(5);
            continue;
        }
      }

      // printf("%s has IPv4 address %u.%u.%u.%u\n", host->h_name, (unsigned char)host->h_addr[0], (unsigned char)host->h_addr[1], (unsigned char)host->h_addr[2], (unsigned char)host->h_addr[3]);
      addr.sin_addr.s_addr = *((unsigned int*)(host->h_addr));
      break;
    }
  }
  if(sscanf(argv[2], "%hu", &(addr.sin_port)) != 1) {
    fprintf(stderr, "Invalid port number %s\n", argv[2]);
    return 1;
  }
  addr.sin_port = htons(addr.sin_port);

  if(connect(s, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) == -1) {
    perror("connect ");
    return 1;
  }
  printf("Connected to %u.%u.%u.%u[%hu]\n", ((unsigned char*)(&addr.sin_addr.s_addr))[0], ((unsigned char*)(&addr.sin_addr.s_addr))[1], ((unsigned char*)(&addr.sin_addr.s_addr))[2], ((unsigned char*)(&addr.sin_addr.s_addr))[3], ntohs(addr.sin_port));

  // Enter raw mode
  if(tcgetattr(0, &origin) == -1) {
    perror("tcgetattr ");
    goto FAILURE_GETATTR_END;
  }

  raw = origin;
  raw.c_iflag &= ~(IXON | ICRNL | INPCK | ISTRIP | BRKINT);
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw.c_cflag |= (CS8);
  raw.c_oflag &= ~(OPOST);

  if(tcsetattr(0, TCSAFLUSH, &raw) == -1) {
    perror("tcsetattr ");
    goto FAILURE_END;
  }

  fd_set rd_set;
  unsigned int len;
  char buff[RECV_BUFF_LEN];
  while(1) {
    FD_ZERO(&rd_set);
    FD_SET(0, &rd_set);
    FD_SET(s, &rd_set);

    if(select(s+1, &rd_set, NULL, NULL, NULL) == -1) {
      perror("select "); goto FAILURE_END;
    }

    if(FD_ISSET(0, &rd_set)) {
      if((len = read(0, buff, RECV_BUFF_LEN)) == -1) {
        perror("read "); goto FAILURE_END;
      }
      if(send(s, buff, len, 0) == -1) {
        perror("send "); goto FAILURE_END;
      }
    }

    if(FD_ISSET(s, &rd_set)) {
      if((len = recv(s, buff, RECV_BUFF_LEN, 0)) <= 0) {
        if(!len) break;
        perror("recv "); goto FAILURE_END;
      }
      if(write(1, buff, len) == -1) {
        perror("write "); goto FAILURE_END;
      }
    }
  }

SUCCESS_END:
  tcsetattr(0, TCSAFLUSH, &origin);
  close(s);
  return 0;
FAILURE_END:
  tcsetattr(0, TCSAFLUSH, &origin);
FAILURE_GETATTR_END:
  close(s);
  return 1;
}
