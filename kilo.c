/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/
struct termios orig_termios;

/*** terminal ***/
void die(const char *s) {
  perror(s);
  exit(1);
}
void disableRawMode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    die("tcsetattr");
}
void enableRawMode(void) {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);
  struct termios raw;
  raw = orig_termios;
  raw.c_iflag &= ~(ICRNL | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_lflag &= ~(IEXTEN | ECHO | ICANON | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

char editorReadKey(void) {
  int nread;
  char c = '\0';
  while ((nread = read(STDIN_FILENO, &c, 1)) !=
         1) { // Here the third parameter in the read function is the max limit
              // bytes that it can capture, the min limit is captured by VMIN.
    // VTIME is the timeout in tenths of a second. So it waits for VMIN bytes to
    // be read, or VTIME tenths of a second to pass.
    if (nread == -1 && errno != EAGAIN) {
      die("read");
    }
  }
  return c;
}

/*** output ***/
void editorRefreshScreen() {

  write(STDOUT_FILENO, "\x1b[2J", 4); // Clear screen escape sequence.
}

/*** input ***/
void editorProcessKeypress(void) {
  char c = editorReadKey();
  switch (c) {
  case CTRL_KEY('e'):
    exit(0);
  }
}

/*** init ***/
int main(void) {
  enableRawMode();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}
