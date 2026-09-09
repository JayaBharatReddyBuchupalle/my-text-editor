/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define TEXT_EDITOR_VERSION "0.0.1"
#define ABUF_INIT {NULL, 0};

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY
};

/*** data ***/
typedef struct erow {
  int size;
  char *chars;
} erow;

struct editorConfig {
  int cx, cy;
  int screenRows;
  int screenCols;
  int numRows;
  erow row;
  struct termios orig_termios;
};
struct editorConfig E;

/*** terminal ***/
void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}
void disableRawMode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode(void) {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);
  struct termios raw;
  raw = E.orig_termios;
  raw.c_iflag &= ~(ICRNL | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_lflag &= ~(IEXTEN | ECHO | ICANON | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int editorReadKey(void) {
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

  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return c;
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return c;

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        while (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~' && seq[1] == '3') {
          return DEL_KEY;
        }
      } else {
        switch (seq[1]) {
        case 'A': // up arrow
          return ARROW_UP;
        case 'B': // down arrow
          return ARROW_DOWN;
        case 'C': // right arrow
          return ARROW_RIGHT;
        case 'D': // left arrow
          return ARROW_LEFT;
        }
      }
    }
    return '\x1b';
  }
  return c;
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%dR", rows, cols) != 2)
    return -1;
  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize sz;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &sz) == -1 || sz.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
      return -1;
    }
    return getCursorPosition(rows, cols);
  } else {
    *cols = sz.ws_col;
    *rows = sz.ws_row;
    return 0;
  }
}

/*** File I/O ***/
void editorOpen(char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  linelen = getline(&line, &linecap, fp);
  if (linelen != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
      linelen--;
    }
    erow e;
    e.size = linelen;
    e.chars = malloc(linelen + 1);
    memcpy(e.chars, line, linelen);
    e.chars[linelen] = '\0';
    E.row = e;
    E.numRows = 1;
  }
  free(line);
  fclose(fp);
}

/*** append buffer ***/
struct abuf {
  char *b;
  int len;
};

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) { free(ab->b); }

/*** output ***/
void editorDrawRows(struct abuf *ab) {
  for (int y = 0; y < E.screenRows; y++) {
    if (y >= E.numRows) {
      if (y == E.screenRows / 3) {
        char welcome[80];
        int welcomelen =
            snprintf(welcome, sizeof(welcome), "Text Editor -- version %s",
                     TEXT_EDITOR_VERSION);
        if (welcomelen > E.screenCols)
          welcomelen = E.screenCols;
        int padding = (E.screenCols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) {
          abAppend(ab, " ", 1);
        }
        abAppend(ab, welcome, welcomelen);

      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row.size;
      if (len > E.screenCols)
        len = E.screenCols;
      abAppend(ab, E.row.chars, len);
    }
    abAppend(ab, "\x1b[K", 3); // Clear line after cursor
    if (y < E.screenRows - 1) {
      abAppend(ab, "\r\n", 2); // newline(/n) and starting position of next
                               // line(/r)
    }
  }
}

void editorRefreshScreen(void) {
  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6); // Hide cursor
  // abAppend(&ab, "\x1b[2J", 4);   // Clear screen escape sequence.
  abAppend(&ab, "\x1b[H", 3); // Move cursor to top-left corner
  // escape sequence.
  editorDrawRows(&ab);
  // abAppend(&ab, "\x1b[H", 3);    // Move cursor to top-left corner
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));
  abAppend(&ab, "\x1b[?25h", 6); // Show cursor
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*** input ***/
void editorMoveCursor(int key) {
  switch (key) {
  case ARROW_UP:
    if (E.cy != 0)
      E.cy--;
    break;
  case ARROW_DOWN:
    if (E.cy != E.screenRows - 1)
      E.cy++;
    break;
  case ARROW_LEFT:
    if (E.cx != 0)
      E.cx--;
    break;
  case ARROW_RIGHT:
    if (E.cx != E.screenCols - 1)
      E.cx++;
    break;
  }
}

void editorProcessKeypress(void) {
  int c = editorReadKey();
  switch (c) {
  case CTRL_KEY('e'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    editorMoveCursor(c);
    break;
  case CTRL_KEY('f'): // Temporary test key to trigger an error
    die("test error");
  }
}

/*** init ***/
void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.numRows = 0;
  if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
    die("getWindowSize");
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}
