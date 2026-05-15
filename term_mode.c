#include <termios.h>

struct termios orig_termios;

void enable_raw_mode(int fd) {
    tcgetattr(fd, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(fd, TCSAFLUSH, &raw);
}

void disable_raw_mode(int fd) {
    tcsetattr(fd, TCSAFLUSH, &orig_termios);
}
