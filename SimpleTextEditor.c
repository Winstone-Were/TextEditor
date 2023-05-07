
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <time.h>
#include <stdarg.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define TAB_STOP 8  

typedef struct  erow
{
  int size;
  int renderSize;
  char *render;
  char *chars;
} erow;


enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

struct editorConfig {
    int cx,cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencolumns;
    int numrows;
    erow *row;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios orig_termions;
};

struct editorConfig E;
/*** Terminal ***/
void die(const char *s) {

    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}


void disableRawMode() {
   if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termions) == -1 )
    die("tcsetattr");
}

void enableRawMode() {
    
    if (tcgetattr(STDIN_FILENO, &E.orig_termions)==-1)
        die("tcgetattr");
    atexit(disableRawMode);
    /*
        using bitwise operations to turn of echoing
        ECHO is a bit flag
        ICANON is also a bit flag we use it to remove canonical mode
            i.e, I don't have to press enter to give input
    */
    struct termios raw = E.orig_termions;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    //TCSAFLUSH waits for all pending output and discard any input that hasn't been read
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw)==-1) die("tcgetattr");
}

int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
            case 'A': return ARROW_UP;
            case 'B': return ARROW_DOWN;
            case 'C': return ARROW_RIGHT;
            case 'D': return ARROW_LEFT;
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
        }
      }

    }else if (seq[0] == 'O')
    {
        switch (seq[1])
        {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
        }
    }
    
    return '\x1b';
  } else {
    return c;
  }

}


int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        return -1;
    }else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    }
}

/* ROW OPERATIONS */

int editorRowCxtoRx(erow *row, int cx){
  int rx = 0;
  int j;
  for(j=0; j<cx; j++) {
    if(row->chars[j] == '\t'){
      rx += 7 - (rx%8);
    }
    rx++;
  }
  return rx;
}


void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;
  free(row->render);
  row->render = malloc(row->size + tabs*(TAB_STOP - 1) + 1);
  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->renderSize = idx;
}

void editorAppendRow(char *s, size_t len) {

  E.row = realloc(E.row, sizeof(erow)*(E.numrows+1));

  int at = E.numrows;

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].renderSize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
}

/* FILE I/O */
void editorOpen(char *filename) {

  free(E.filename);
  E.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  //linelen = getline(&line, &linecap, fp);
  while ((linelen = getline(&line, &linecap, fp)) != -1){
    while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
        linelen--;
    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
}

/*** append buffer ***/

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL,0}

void abAppend(struct abuf *ab, const char *s, int len) {
    
    char *new = realloc(ab->b, ab->len + len);

    if( new == NULL) return;
    memcpy(&new[ab->len],s,len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab){
    free(ab->b);
}

/*** input ***/

void editorMoveCursor(int key) {

  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      }else if (E.cy > 0){
        E.cy--;
        E.cy = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row->size) {
        E.cx++;
      } else if(row && E.cx == row->size){
        //continue to next line if you've reached furthest right
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows) {
        E.cy++;
      }
      break;
  }

  row = (E.cx >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if(E.cx > rowlen){
    E.cx = rowlen;
  }

}
//Handling key press
void editorProcessKeypress() {
    int c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case HOME_KEY:
            E.cx = 0;
            break;
        
        case END_KEY:
            if(E.cy < E.numrows)
              E.cx = E.row[E.cy].size;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
          {
            if( c == PAGE_UP) {
              E.cy = E.rowoff;
            }else if ( c == PAGE_DOWN) {
              E.cy = E.rowoff + E.screenrows - 1;
              if( E.cy > E.numrows ) E.cy = E.numrows;
            }
          }
        {
            int times = E.screenrows;
            while (times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;  

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/* OUTPUT */
void editorScroll() {

  E.rx = 0;
  if(E.cy < E.numrows) {
    E.rx = editorRowCxtoRx(&E.row[E.cy], E.cx);
  }

  if(E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if(E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows+1;
  }
  if(E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if(E.rx >= E.coloff + E.screencolumns){
    E.coloff = E.rx - E.screencolumns + 1;
  }
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines",
    E.filename ? E.filename : "[No Name]", E.numrows);
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
    E.cy + 1, E.numrows);
  if (len > E.screencolumns) len = E.screencolumns;
  abAppend(ab, status, len);
  while (len < E.screencolumns) {
    if (E.screencolumns - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab){
  abAppend(ab, "\x1b[k", 3);
  int msglen = strlen(E.statusmsg);
  if(msglen > E.screencolumns) msglen = E.screencolumns;
  if(msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;

    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Simple Text Editor");
        if (welcomelen > E.screencolumns) welcomelen = E.screencolumns;
        int padding = (E.screencolumns - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].renderSize - E.coloff;
      if(len < 0) len = 0;
      if (len > E.screencolumns) len = E.screencolumns;
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }

    abAppend(ab, "\x1b[K", 3);
    //if (y < E.screenrows - 1) {
    abAppend(ab, "\r\n", 2);
    //}
  }
}

void editorRefreshScreen() {

  editorScroll();
    /*
        sending an escape sequence to the terminal
            they start with \x1b followed by '['
        The J -> erases the display
            2 is an argument to clear the whole screen        
    */

   struct abuf ab = ABUF_INIT;

    abAppend(&ab,"\x1b[?25l",6);
    //abAppend(&ab,"\x1b[2J",4);
    abAppend(&ab,"\x1b[H",3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,(E.rx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h",6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...){
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

void initEditor() {
  
  E.cx = 0;
  E.cy = 0;
  //scrolled to the top by default
  E.rx = 0;
  E.coloff = 0;
  E.rowoff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.filename = NULL; 
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  if(getWindowSize(&E.screenrows, &E.screencolumns) == -1) die("getWindowSize");
  E.screenrows -= 2;
}


int main(int argc, char *argv[]) {

    enableRawMode();
    initEditor();
    if(argc >= 2) {
      editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl+Q = quit");

    while(1) {
        editorRefreshScreen();
        editorProcessKeypress();

    }

    return 0;
}