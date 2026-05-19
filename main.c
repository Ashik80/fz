#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/select.h>
#include "term_escapes.h"
#include "term_mode.h"
#include "fuzzy.h"

#define INITIAL_CAPACITY 10
#define PROMPT_FL "-p"
#define EMPTY_ITEM_LIST (ItemList){ .items = NULL, .count = 0, .capacity = 0 }

typedef struct {
    char *name;
    int score;
} Item;

Item *new_item(char *name) {
    Item *item = malloc(sizeof(Item));
    if (!item) {
        perror("Failed to allocate memory for item");
        return NULL;
    }
    item->name = strdup(name);
    item->score = 0;
    return item;
}

void free_item(Item *item) {
    free(item->name);
    free(item);
}

typedef struct {
    Item **items;
    size_t count;
    size_t capacity;
} ItemList;

ItemList new_item_list() {
    Item **items = malloc(sizeof(Item *) * INITIAL_CAPACITY);
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

void item_list_add_item(ItemList *item_list, Item *item) {
    if (item_list->count >= item_list->capacity) {
        item_list->capacity *= 2;
        Item **items = realloc(item_list->items, sizeof(Item *) * item_list->capacity);
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
        free_item(item_list->items[i]);
    }
    free(item_list->items);
}

typedef struct {
    ItemList item_list;
    int pipefd[2];
    char *prompt;
    char *query;
    FILE *tty;
    size_t cursor;
    int tty_fd;
    int selected;
    int offset;
    int rows;
    int rows_to_be_printed;
    int cols;
    int loading_done;
    pthread_t list_loader_thread;
    pthread_t list_sorter_thread;
    pthread_mutex_t list_modifier_mutex;
    pthread_cond_t list_modifier_cond;
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
        .cursor = cursor,
        .tty_fd = tty_fd,
        .selected = 0,
        .offset = 0,
        .rows = w.ws_row - 3,
        .rows_to_be_printed = 0,
        .cols = w.ws_col,
        .loading_done = 0,
        .list_loader_thread = 0,
        .list_sorter_thread = 0,
        .list_modifier_mutex = PTHREAD_MUTEX_INITIALIZER,
        .list_modifier_cond = PTHREAD_COND_INITIALIZER
    };
}

void free_fz_state(FzState *fz_state) {
    free_item_list(&fz_state->item_list);
    free(fz_state->prompt);
    free(fz_state->query);
    fclose(fz_state->tty);
}

void fz_state_add_prompt_text(FzState *fz_state, char *prompt) {
    fz_state->prompt = prompt;
    fz_state->cursor = strlen(prompt) + strlen(fz_state->query) + 2;
}

int fz_state_calculate_rows(FzState *fz_state) {
    fz_state->rows_to_be_printed = fz_state->item_list.count > fz_state->rows ? fz_state->rows : fz_state->item_list.count;
    return fz_state->rows_to_be_printed;
}

void fz_state_print_item_list(FzState *fz_state) {
    int rows_to_be_printed = fz_state_calculate_rows(fz_state);
    for (size_t i = fz_state->offset; i <= rows_to_be_printed + fz_state->offset - 1; i++) {
        if (fz_state->item_list.items[i]->score != NO_MATCH) {
            int cols = fz_state->cols - 3;
            size_t len = strlen(fz_state->item_list.items[i]->name);
            char *copied_name = strdup(fz_state->item_list.items[i]->name);
            char *text = copied_name;
            size_t diff = 0;
            if (len > cols) {
                diff = len - cols;
                text = copied_name + diff;
                text[0] = '.';
                text[1] = '.';
                text[2] = '.';
            }
            if (i == fz_state->selected) {
                fprintf(fz_state->tty, "\033[1m> %s\033[0m\n", text);
            } else {
                fprintf(fz_state->tty, "  %s\n", text);
            }
            free(copied_name);
        }
    }
}

void fz_state_print_divider(FzState *fz_state) {
    for (int i = 0; i < fz_state->cols; i++) {
        fprintf(fz_state->tty, "—");
    }
}

static int cmpitemp(const void *p1, const void *p2) {
    Item *item1 = *(Item **) p1;
    Item *item2 = *(Item **) p2;
    return item2->score - item1->score;
}

void fz_state_fuzzy_sort_item_list(FzState *fz_state) {
    for (size_t i = 0; i < fz_state->item_list.count; i++) {
        int score = fuzzy_score(fz_state->item_list.items[i]->name, fz_state->query);
        fz_state->item_list.items[i]->score = score;
    }
    qsort(fz_state->item_list.items, fz_state->item_list.count, sizeof(Item *), cmpitemp);
}

void *fuzzy_sort_item_in_thread(void *args) {
    FzState *fz_state = (FzState *)args;
    pthread_mutex_lock(&fz_state->list_modifier_mutex);
    fz_state_fuzzy_sort_item_list(fz_state);
    pthread_mutex_unlock(&fz_state->list_modifier_mutex);
    write(fz_state->pipefd[1], "x", 1);
    return NULL;
}

void fz_state_render(FzState *fz_state) {
    clear_screen(fz_state->tty);
    fprintf(fz_state->tty, "%s %s\n", fz_state->prompt, fz_state->query);
    fz_state_print_divider(fz_state);
    fz_state_print_item_list(fz_state);
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
    fz_state->selected = 0;
    fz_state->offset = 0;
    if (fz_state->list_sorter_thread) {
        pthread_join(fz_state->list_sorter_thread, NULL);
        fz_state->list_sorter_thread = 0;
    }
    pthread_create(&fz_state->list_sorter_thread, NULL, fuzzy_sort_item_in_thread, fz_state);
}

void fz_state_query_remove_char_before_cursor(FzState *fz_state) {
    size_t prompt_len = strlen(fz_state->prompt) + 1;
    if (fz_state->cursor <= prompt_len + 1) return;
    size_t char_index = fz_state->cursor - (prompt_len + 2);
    size_t last_index = strlen(fz_state->query) - 1;
    memmove(&fz_state->query[char_index], &fz_state->query[char_index + 1], last_index - char_index);
    fz_state->query[last_index] = '\0';
    fz_state->cursor--;
    fz_state->selected = 0;
    fz_state->offset = 0;
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
    fz_state->selected = 0;
    fz_state->offset = 0;
}

void fz_state_query_remove_from_cursor_to_end(FzState *fz_state) {
    size_t prompt_len_with_space = strlen(fz_state->prompt) + 2;
    size_t cursor_index = fz_state->cursor - prompt_len_with_space;
    if (cursor_index >= strlen(fz_state->query)) return;
    fz_state->query[cursor_index] = '\0';
    fz_state->selected = 0;
    fz_state->offset = 0;
}

void fz_state_move_selected_up(FzState *fz_state) {
    if (fz_state->selected > 0) {
        if (fz_state->offset >= fz_state->selected) {
            fz_state->offset--;
        }
        fz_state->selected--;
    }
}

void fz_state_move_selected_down(FzState *fz_state) {
    int rows_to_be_printed = fz_state_calculate_rows(fz_state);
    if (fz_state->selected < fz_state->item_list.count - 1) {
        if (fz_state->item_list.items[fz_state->selected + 1]->score == NO_MATCH) return;
        fz_state->selected++;
        if (fz_state->selected > fz_state->offset + rows_to_be_printed - 1) {
            fz_state->offset = (fz_state->selected + 1) - rows_to_be_printed;
        }
    }
}

char *fz_state_select_from_item(FzState *fz_state) {
    char *selected = NULL;
    selected = strdup(fz_state->item_list.items[fz_state->selected]->name);
    return selected;
}

void fz_state_load_items_from_pipe(FzState *fz_state) {
    char *buf = NULL;
    size_t buf_len = 0;
    while (getline(&buf, &buf_len, stdin) != -1) {
        if (buf[strlen(buf) - 1] == '\n')
            buf[strlen(buf) - 1] = '\0';
        pthread_mutex_lock(&fz_state->list_modifier_mutex);
        item_list_add_item(&fz_state->item_list, new_item(buf));
        if (fz_state->item_list.count > fz_state->rows) {
            fz_state->loading_done = 1;
            pthread_cond_signal(&fz_state->list_modifier_cond);
        }
        pthread_mutex_unlock(&fz_state->list_modifier_mutex);
    }
    free(buf);
}

void *load_items_from_pipe_thread(void *args) {
    FzState *fz_state = (FzState *)args;
    fz_state_load_items_from_pipe(fz_state);
    pthread_mutex_lock(&fz_state->list_modifier_mutex);
    fz_state->loading_done = 1;
    pthread_cond_signal(&fz_state->list_modifier_cond);
    pthread_mutex_unlock(&fz_state->list_modifier_mutex);
    return NULL;
}

int is_directory(char *path) {
    struct stat sb;
    if (lstat(path, &sb) == -1) {
        perror("Failed to stat file");
        return 0;
    }
    return S_ISDIR(sb.st_mode);
}

void fz_state_load_items_from_directory(FzState *fz_state, char *base_dir) {
    DIR *dir = opendir(base_dir);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".git") == 0)
            continue;
        size_t path_len = strlen(base_dir) + strlen(entry->d_name) + 2;
        char *path = malloc(path_len);
        snprintf(path, path_len, "%s/%s", base_dir, entry->d_name);
        if (is_directory(path)) {
            fz_state_load_items_from_directory(fz_state, path);
        } else {
            pthread_mutex_lock(&fz_state->list_modifier_mutex);
            item_list_add_item(&fz_state->item_list, new_item(path));
            if (fz_state->item_list.count > fz_state->rows) {
                fz_state->loading_done = 1;
                pthread_cond_signal(&fz_state->list_modifier_cond);
            }
            pthread_mutex_unlock(&fz_state->list_modifier_mutex);
        }
        free(path);
    }
    closedir(dir);
}

typedef struct {
    FzState *fz_state;
    char *base_dir;
} LoadFromDirThreadArgs;

LoadFromDirThreadArgs *new_load_from_dir_thread_args(FzState *fz_state, char *base_dir) {
    LoadFromDirThreadArgs *args = malloc(sizeof(LoadFromDirThreadArgs));
    args->fz_state = fz_state;
    args->base_dir = strdup(base_dir);
    return args;
}

void *load_item_from_directory_in_thread(void *args) {
    LoadFromDirThreadArgs *dargs = (LoadFromDirThreadArgs *)args;
    fz_state_load_items_from_directory(dargs->fz_state, dargs->base_dir);
    pthread_mutex_lock(&dargs->fz_state->list_modifier_mutex);
    dargs->fz_state->loading_done = 1;
    pthread_cond_signal(&dargs->fz_state->list_modifier_cond);
    pthread_mutex_unlock(&dargs->fz_state->list_modifier_mutex);
    free(dargs->base_dir);
    free(dargs);
    return NULL;
}

static FzState fz_state;

void exit_early(int sig) {
    if (fz_state.list_loader_thread) {
        pthread_cancel(fz_state.list_loader_thread);
        pthread_join(fz_state.list_loader_thread, NULL);
    }
    if (fz_state.list_sorter_thread) {
        pthread_cancel(fz_state.list_sorter_thread);
        pthread_join(fz_state.list_sorter_thread, NULL);
    }
    disable_raw_mode(fz_state.tty_fd);
    exit_alternate_buffer(fz_state.tty);
    free_fz_state(&fz_state);
    exit(0);
}

int main(int argc, char **argv) {
    signal(SIGINT, exit_early);

    char *prompt = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], PROMPT_FL) == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing argument for -p\n");
                exit(1);
            }
            prompt = argv[i + 1];
            break;
        }
    }

    fz_state = new_fz_state();
    if (prompt) {
        free(fz_state.prompt);
        fz_state_add_prompt_text(&fz_state, strdup(prompt));
    }

    enter_alternate_buffer(fz_state.tty);
    enable_raw_mode(fz_state.tty_fd);

    if (isatty(STDIN_FILENO)) {
        LoadFromDirThreadArgs *args = new_load_from_dir_thread_args(&fz_state, ".");
        pthread_create(&fz_state.list_loader_thread, NULL, load_item_from_directory_in_thread, args);
    } else {
        pthread_create(&fz_state.list_loader_thread, NULL, load_items_from_pipe_thread, &fz_state);
    }

    char c;
    char *result = NULL;
    int exit_code = 0;
    int no_items = 0;
    pipe(fz_state.pipefd);

    pthread_mutex_lock(&fz_state.list_modifier_mutex);
    while (!fz_state.loading_done) {
        pthread_cond_wait(&fz_state.list_modifier_cond, &fz_state.list_modifier_mutex);
        if (fz_state.item_list.count == 0) {
            no_items = 1;
            exit_code = 1;
            break;
        }
        fz_state_render(&fz_state);
    }
    if (no_items) goto cleanup;
    pthread_mutex_unlock(&fz_state.list_modifier_mutex);

    while (1) {
        pthread_mutex_lock(&fz_state.list_modifier_mutex);
        fz_state_render(&fz_state);
        pthread_mutex_unlock(&fz_state.list_modifier_mutex);

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fz_state.tty_fd, &fds);
        FD_SET(fz_state.pipefd[0], &fds);
        int max_fd = (fz_state.tty_fd > fz_state.pipefd[0] ? fz_state.tty_fd : fz_state.pipefd[0]) + 1;
        select(max_fd, &fds, NULL, NULL, NULL);
        if (FD_ISSET(fz_state.pipefd[0], &fds)) {
            char pc;
            read(fz_state.pipefd[0], &pc, 1);
            continue;
        }

        int res = read(fz_state.tty_fd, &c, 1);
        if (res == -1) {
            exit_code = 1;
            goto cleanup;
        }
        if (c == '\n') {
            pthread_mutex_lock(&fz_state.list_modifier_mutex);
            result = fz_state_select_from_item(&fz_state);
            pthread_mutex_unlock(&fz_state.list_modifier_mutex);
            goto cleanup;
        } else if (c == '\033') {
            char c2[2];
            read(fz_state.tty_fd, &c2[0], 1);
            read(fz_state.tty_fd, &c2[1], 1);
            if (c2[0] == '[') {
                if (c2[1] == 'A') {
                    pthread_mutex_lock(&fz_state.list_modifier_mutex);
                    fz_state_move_selected_up(&fz_state);
                    pthread_mutex_unlock(&fz_state.list_modifier_mutex);
                } else if (c2[1] == 'B') {
                    pthread_mutex_lock(&fz_state.list_modifier_mutex);
                    fz_state_move_selected_down(&fz_state);
                    pthread_mutex_unlock(&fz_state.list_modifier_mutex);
                } else if (c2[1] == 'C') {
                    pthread_mutex_lock(&fz_state.list_modifier_mutex);
                    fz_state_move_cursor_right(&fz_state);
                    pthread_mutex_unlock(&fz_state.list_modifier_mutex);
                } else if (c2[1] == 'D') {
                    pthread_mutex_lock(&fz_state.list_modifier_mutex);
                    fz_state_move_cursor_left(&fz_state);
                    pthread_mutex_unlock(&fz_state.list_modifier_mutex);
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
    if (fz_state.list_loader_thread) {
        pthread_cancel(fz_state.list_loader_thread);
        pthread_join(fz_state.list_loader_thread, NULL);
    }
    if (fz_state.list_sorter_thread) {
        pthread_cancel(fz_state.list_sorter_thread);
        pthread_join(fz_state.list_sorter_thread, NULL);
    }
    disable_raw_mode(fz_state.tty_fd);
    exit_alternate_buffer(fz_state.tty);
    free_fz_state(&fz_state);
    if (!no_items) {
        printf("%s\n", result);
    }
    free(result);
    return exit_code;
}
