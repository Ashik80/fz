#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <unistd.h>
#include "term_escapes.h"
#include "term_mode.h"

#define INITIAL_CAPACITY 10
#define EMPTY_ITEM_LIST (ItemList){ .items = NULL, .count = 0, .capacity = 0 }

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} ItemList;

ItemList new_item_list() {
    char **items = malloc(sizeof(char *) * INITIAL_CAPACITY);
    if (!items) {
        perror("Failed to allocate memory for item list");
        return EMPTY_ITEM_LIST;
    }
    return (ItemList) {
        .items = items,
        .count = 0,
        .capacity = INITIAL_CAPACITY
    };
}

void item_list_add_item(ItemList *item_list, char *item) {
    if (item_list->count >= item_list->capacity) {
        item_list->capacity *= 2;
        char **items = realloc(item_list->items, sizeof(char *) * item_list->capacity);
        if (!items) {
            perror("Failed to allocate memory for item list");
            return;
        }
        item_list->items = items;
    }
    item_list->items[item_list->count] = item;
    item_list->count++;
}

void free_item_list(ItemList *item_list) {
    for (size_t i = 0; i < item_list->count; i++) {
        free(item_list->items[i]);
    }
    free(item_list->items);
}

typedef struct {
    ItemList item_list;
    char *prompt;
    char *query;
    FILE *tty;
    int tty_fd;
    size_t cursor;
    int rows;
    int cols;
} FzState;

FzState new_fz_state() {
    struct winsize w;
    FILE *tty = fopen("/dev/tty", "r+");
    int tty_fd = fileno(tty);
    ioctl(tty_fd, TIOCGWINSZ, &w);
    char *prompt = "Query>";
    char *query = "";
    size_t cursor = strlen(prompt) + strlen(query) + 2;
    return (FzState) {
        .prompt = strdup(prompt),
        .query = strdup(query),
        .item_list = new_item_list(),
        .tty = tty,
        .tty_fd = tty_fd,
        .cursor = cursor,
        .rows = w.ws_row,
        .cols = w.ws_col
    };
}

void free_fz_state(FzState *fz_state) {
    free_item_list(&fz_state->item_list);
    free(fz_state->prompt);
    free(fz_state->query);
    fclose(fz_state->tty);
}

void render_fz(FzState *fz_state) {
    clear_screen(fz_state->tty);
    fprintf(fz_state->tty, "%s %s\n", fz_state->prompt, fz_state->query);
    for (int i = 0; i < fz_state->cols; i++) {
        fprintf(fz_state->tty, "—");
    }
    // DEBUG print
    // DEBUG print
    fprintf(fz_state->tty, "\n");
    fprintf(fz_state->tty, "\033[1;%zuH", fz_state->cursor);
    fflush(fz_state->tty);
}

void fz_state_update_query(FzState *fz_state, char c) {
    char *query = realloc(fz_state->query, strlen(fz_state->query) + 2);
    if (!query) {
        perror("Failed to allocate memory for query");
        return;
    }
    fz_state->query = query;
    size_t prompt_len_with_space = strlen(fz_state->prompt) + 2;
    size_t cursor_index = fz_state->cursor - prompt_len_with_space;
    size_t query_len = strlen(fz_state->query);
    memmove(&fz_state->query[cursor_index + 1], &fz_state->query[cursor_index], query_len - cursor_index);
    fz_state->query[cursor_index] = c;
    fz_state->query[query_len + 1] = '\0';
    fz_state->cursor++;
}

void fz_state_query_remove_char_before_cursor(FzState *fz_state) {
    size_t prompt_len = strlen(fz_state->prompt) + 1;
    if (fz_state->cursor <= prompt_len + 1) return;
    size_t char_index = fz_state->cursor - (prompt_len + 2);
    size_t last_index = strlen(fz_state->query) - 1;
    memmove(&fz_state->query[char_index], &fz_state->query[char_index + 1], last_index - char_index);
    fz_state->query[last_index] = '\0';
    fz_state->cursor--;
}

void fz_state_move_cursor_left(FzState *fz_state) {
    size_t prompt_len = strlen(fz_state->prompt) + 1;
    if (fz_state->cursor > prompt_len + 1) {
        fz_state->cursor--;
    }
}

void fz_state_move_cursor_right(FzState *fz_state) {
    size_t query_line_len = strlen(fz_state->prompt) + strlen(fz_state->query) + 1;
    if (fz_state->cursor < query_line_len + 1) {
        fz_state->cursor++;
    }
}

void fz_state_move_cursor_to_start(FzState *fz_state) {
    fz_state->cursor = strlen(fz_state->prompt) + 2;
}

void fz_state_move_cursor_to_end(FzState *fz_state) {
    fz_state->cursor = strlen(fz_state->prompt) + strlen(fz_state->query) + 2;
}

void fz_state_query_remove_from_cursor_to_start(FzState *fz_state) {
    size_t prompt_len_with_space = strlen(fz_state->prompt) + 2;
    size_t cursor_index = fz_state->cursor - prompt_len_with_space;
    size_t start_index = prompt_len_with_space;
    size_t bytes_to_move = strlen(fz_state->query) - cursor_index;
    memmove(&fz_state->query[0], &fz_state->query[cursor_index], bytes_to_move);
    fz_state->cursor = start_index;
    fz_state->query[bytes_to_move] = '\0';
}

void fz_state_query_remove_from_cursor_to_end(FzState *fz_state) {
    size_t prompt_len_with_space = strlen(fz_state->prompt) + 2;
    size_t cursor_index = fz_state->cursor - prompt_len_with_space;
    if (cursor_index >= strlen(fz_state->query)) return;
    fz_state->query[cursor_index] = '\0';
}

static FzState fz_state;

void exit_early(int sig) {
    disable_raw_mode(fz_state.tty_fd);
    exit_alternate_buffer(fz_state.tty);
    free_fz_state(&fz_state);
    printf("Exited early with signal %d\n", sig);
    exit(0);
}

int main() {
    signal(SIGINT, exit_early);

    fz_state = new_fz_state();
    enter_alternate_buffer(fz_state.tty);
    enable_raw_mode(fz_state.tty_fd);
    char c;
    while (1) {
        render_fz(&fz_state);
        int res = read(fz_state.tty_fd, &c, 1);
        if (res == -1) goto cleanup;
        if (c == '\n') {
            goto cleanup;
        } else if (c == '\033') {
            char c2[2];
            read(fz_state.tty_fd, &c2[0], 1);
            read(fz_state.tty_fd, &c2[1], 1);
            if (c2[0] == '[') {
                if (c2[1] == 'C') {
                    fz_state_move_cursor_right(&fz_state);
                } else if (c2[1] == 'D') {
                    fz_state_move_cursor_left(&fz_state);
                }
            }
        } else if (c == 0x01) { // ctrl-a
            fz_state_move_cursor_to_start(&fz_state);
        } else if (c == 0x05) { // ctrl-e
            fz_state_move_cursor_to_end(&fz_state);
        } else if (c == 0x06) { // ctrl-f
            fz_state_move_cursor_right(&fz_state);
        } else if (c == 0x02) { // ctrl-b
            fz_state_move_cursor_left(&fz_state);
        } else if (c == 0x15) { // ctrl-u
            fz_state_query_remove_from_cursor_to_start(&fz_state);
        } else if (c == 0x0B) { // ctrl-k
            fz_state_query_remove_from_cursor_to_end(&fz_state);
        } else if (c == 127) {
            fz_state_query_remove_char_before_cursor(&fz_state);
        } else {
            fz_state_update_query(&fz_state, c);
        }
    }

cleanup:
    disable_raw_mode(fz_state.tty_fd);
    exit_alternate_buffer(fz_state.tty);
    free_fz_state(&fz_state);
    printf("Exited cleanly\n");
    return 0;
}
