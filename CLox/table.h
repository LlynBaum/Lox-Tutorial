#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

typedef struct {
    Value key;
    Value value;
} Entry;

typedef struct {
    int count;
    int capacity;
    Entry *entries;
} Table;

#define TABLE_MAX_LOAD 0.75

void initTable(Table *table);

void freeTable(Table *table);

bool tableGet(const Table *table, Value key, Value *value);

bool tableSet(Table *table, Value key, Value value);

bool tableDelete(const Table *table, Value key);

void tableAddAll(const Table *from, Table *to);

ObjString *tableFindString(const Table *table, const char *chars, int length, uint32_t hash);

void markTable(Table *table);

void tableRemoveWhiet(Table *table);

#endif //clox_table_h
