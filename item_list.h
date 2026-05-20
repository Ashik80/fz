#ifndef ITEM_H
#define ITEM_H

#define INITIAL_CAPACITY 10
#define EMPTY_ITEM_LIST (ItemList){ .items = NULL, .count = 0, .capacity = 0 }

typedef struct {
    char *name;
    int score;
} Item;

typedef struct {
    Item **items;
    size_t count;
    size_t capacity;
} ItemList;

Item *new_item(char *name);
void free_item(Item *item);
ItemList new_item_list();
void item_list_add_item(ItemList *item_list, Item *item);
void free_item_list(ItemList *item_list);

#endif
