#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

#ifndef ZN_TEST_RUNTIME
#error "ZN_TEST_RUNTIME must point at the test runtime directory"
#endif

#define SOCKET_PATH ZN_TEST_RUNTIME "/cp64.sock"

static ssize_t send_fd(int socket_fd, int fd) {
  char control[CMSG_SPACE(sizeof(int))];
  char byte = 0;
  struct iovec iov = { .iov_base = &byte, .iov_len = 1 };
  struct msghdr message = {
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = control,
    .msg_controllen = sizeof(control),
  };

  struct cmsghdr *header = CMSG_FIRSTHDR(&message);
  header->cmsg_len = CMSG_LEN(sizeof(int));
  header->cmsg_level = SOL_SOCKET;
  header->cmsg_type = SCM_RIGHTS;
  memcpy(CMSG_DATA(header), &fd, sizeof(fd));

  return sendmsg(socket_fd, &message, 0);
}

static size_t read_size(int fd) {
  size_t value = 0;
  if (read(fd, &value, sizeof(value)) != (ssize_t)sizeof(value)) return 0;

  return value;
}

static int read_string(int fd, char *out, size_t capacity) {
  size_t length = read_size(fd);
  if (length == 0 || length > capacity) return -1;
  if (read(fd, out, length) != (ssize_t)length) return -1;
  out[length - 1] = '\0';

  return 0;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <module-path> <companion 0|1>\n", argv[0]);
    return 2;
  }

  struct sockaddr_un address = { .sun_family = AF_UNIX };
  snprintf(address.sun_path, sizeof(address.sun_path), "%s", SOCKET_PATH);
  unlink(SOCKET_PATH);

  int server = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server == -1 || bind(server, (struct sockaddr *)&address, sizeof(address)) == -1 ||
      listen(server, 8) == -1) {
    perror("listen");
    return 1;
  }

  printf("ready\n");
  fflush(stdout);

  while (1) {
    int client = accept(server, NULL, NULL);
    if (client == -1) return 1;

    uint8_t action = 0;
    if (read(client, &action, 1) != 1) {
      close(client);
      continue;
    }

    char process[256];
    char exe[4096];

    if (action == 9) {
      if (read_string(client, process, sizeof(process)) == -1 ||
          read_string(client, exe, sizeof(exe)) == -1) {
        close(client);
        continue;
      }
      printf("plan %s %s\n", process, exe);
      fflush(stdout);

      size_t count = 1;
      write(client, &count, sizeof(count));

      uint8_t companion = (uint8_t)atoi(argv[2]);
      write(client, &companion, 1);

      size_t length = strlen(argv[1]);
      write(client, &length, sizeof(length));
      write(client, argv[1], length);

      int library = open(argv[1], O_RDONLY | O_CLOEXEC);
      if (library == -1 || send_fd(client, library) == -1) {
        fprintf(stderr, "failed to send library\n");
        return 1;
      }
      close(library);
    } else if (action == 10) {
      if (read_string(client, exe, sizeof(exe)) == -1) {
        close(client);
        continue;
      }
      printf("companion %s\n", exe);
      fflush(stdout);

      uint8_t ok = 1;
      write(client, &ok, 1);

      char byte = 0;
      if (read(client, &byte, 1) == 1 && byte == 'Q') write(client, "A", 1);
    }

    close(client);
  }
}
