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
    // free(item_list);
}

typedef struct {
    ItemList item_list;
    char *prompt;
    char *query;
    FILE *tty;
    int tty_fd;
    int rows;
    int cols;
} FzState;

FzState new_fz_state() {
    struct winsize w;
    FILE *tty = fopen("/dev/tty", "r+");
    int tty_fd = fileno(tty);
    ioctl(tty_fd, TIOCGWINSZ, &w);
    return (FzState) {
        .prompt = strdup("Query>"),
        .query = strdup(""),
        .item_list = new_item_list(),
        .tty = tty,
        .tty_fd = tty_fd,
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
    size_t query_line_len = strlen(fz_state->prompt) + strlen(fz_state->query) + 2;
    fprintf(fz_state->tty, "%s %s\n", fz_state->prompt, fz_state->query);
    for (int i = 0; i < fz_state->cols; i++) {
        fprintf(fz_state->tty, "-");
    }
    fprintf(fz_state->tty, "\n");
    fprintf(fz_state->tty, "\033[1;%zuH", query_line_len);
    fflush(fz_state->tty);
}

void fz_state_update_query(FzState *fz_state, char c) {
    char *query = realloc(fz_state->query, strlen(fz_state->query) + 2);
    if (!query) {
        perror("Failed to allocate memory for query");
        return;
    }
    fz_state->query = query;
    size_t query_len = strlen(fz_state->query);
    fz_state->query[query_len] = c;
    fz_state->query[query_len + 1] = '\0';
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
