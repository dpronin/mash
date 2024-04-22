/* A sample program provided as local executable.
 * - Once started, this program need to receive SIGINT N times
 *   to complete.
 * - Each time a SIGINT is received, it will print a hello msg.
 */

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/types.h>

#include <unistd.h>
#include <wait.h>

#define N 3
int count = 0;

void sigint_handler(int sig) {
  printf("hello %d from my_pause!\n", ++count);
  fflush(stdout);
}

int main(void) {
  struct sigaction sa{};
  sa.sa_handler = sigint_handler;
  sigaction(SIGINT, &sa, NULL);

  while (count < N)
    pause();

  return 0;
}
