#include <stdio.h>

void enter_alternate_buffer(FILE *tty) {
    fprintf(tty, "\033[?1049h");
}

void exit_alternate_buffer(FILE *tty) {
    fprintf(tty, "\033[?1049l");
}

void hide_cursor(FILE *tty) {
    fprintf(tty, "\033[?25l");
}

void show_cursor(FILE *tty) {
    fprintf(tty, "\033[?25h");
}

void clear_screen(FILE *tty) {
    fprintf(tty, "\033[H\033[J");
}
