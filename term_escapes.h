#ifndef TERM_ESCAPES_H
#define TERM_ESCAPES_H

void enter_alternate_buffer(FILE *tty);

void exit_alternate_buffer(FILE *tty);

void hide_cursor(FILE *tty);

void show_cursor(FILE *tty);

void clear_screen(FILE *tty);

#endif
