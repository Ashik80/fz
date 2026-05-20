#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "item_list.h"

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
