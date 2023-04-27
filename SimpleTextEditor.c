
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>

#define CTRL_KEY(k) ((k) & 0x1f)

//Turn of echoing
/*
    *tcgetattr() raead terminal attributes into a struct
    *tcsetattr() set terminanal attributes from a struct
*/
//c_lflag=>local flags

struct editorConfig {
    int screenrows;
    int screencolumns;
    struct termios orig_termions;
};

struct editorConfig E;


void die(const char *s) {

    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void initEditor() {
    if(getWindowSize(&E.screenrows, &E.screencolumns) == -1) {
        die("getWindowSize");
    }
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

char editorReadKey() {
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c,1)) !=1) {
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    return  c;
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

/*** append buffer ***/

struct abuf {
    char *b;
    int len
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

//Handling key press
void editorProcessKeypress() {
    char c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

void editorDrawRows(struct abuf *ab) {
    int i;
    for(i = 0; i < E.screenrows; i++){
        // writing the tu '~' like vim does

        if(i == E.screenrows / 3) {
            
            char welcome[80];

            int welcomelen = snprintf(welcome, sizeof(welcome), "Text Editor");

            if(welcomelen > E.screencolumns) welcomelen = E.screencolumns;

            abAppend(ab, "~", 1);

        } else{
            abAppend(ab, "~", 1);
        }
        abAppend(ab, "\x1b[K",3);
        if(i < E.screenrows -1) {
            abAppend(ab, "\r\n",2);
        }

    }
}

void editorRefreshScreen() {
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

    abAppend(&ab, "\x1b[H", 3);
    abAppend(&ab, "\x1b[?25h",6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

int main() {

    enableRawMode();
    initEditor();

    while(1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}