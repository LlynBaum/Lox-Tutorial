#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"


void initTable(Table *table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(Table *table) {
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}

static uint32_t hashDouble(const double value) {
    union BitCast {
        double value;
        uint32_t ints[2];
    };

    union BitCast cast;
    cast.value = value + 1.0;
    return cast.ints[0] + cast.ints[1];
}

static uint32_t hashValue(const Value value) {
    switch (value.type) {
        case VAL_BOOL: return AS_BOOL(value) ? 3 : 5;
        case VAL_NIL: return 7;
        case VAL_NUMBER: return hashDouble(AS_NUMBER(value));
        case VAL_OBJ: return AS_STRING(value)->hash;
        case VAL_EMPTY: return 0;
        default: return -1; // Unreachable
    }
}

static Entry *findEntry(Entry *entries, const int capacity, const Value key) {
    uint32_t index = hashValue(key) % capacity;
    Entry *tombstone = NULL;

    for (;;) {
        Entry *entry = &entries[index];
        if (entry->key.type == VAL_EMPTY) {
            if (IS_NIL(entry->value)) {
                return tombstone != NULL ? tombstone : entry;
            }
            if (tombstone == NULL) tombstone = entry;
        } else if (valuesEqual(key, entry->key)) {
            return entry;
        }

        index = (index + 1) % capacity;
    }
}

bool tableGet(const Table *table, const Value key, Value *value) {
    if (table->count == 0) return false;
    const Entry *entry = findEntry(table->entries, table->capacity, key);
    if (entry->key.type == VAL_EMPTY) return false;
    *value = entry->value;
    return true;
}

static void adjustCapacity(Table *table, const int capacity) {
    Entry *entries = ALLOCATE(Entry, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = EMPTY_VAL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        const Entry *entry = &table->entries[i];
        if (entry->key.type == VAL_EMPTY) continue;
        Entry *dest = findEntry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(Entry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool tableSet(Table *table, const Value key, const Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        const int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }

    Entry *entry = findEntry(table->entries, table->capacity, key);
    const bool isNewKey = entry->key.type == VAL_EMPTY;
    if (isNewKey && IS_NIL(entry->value)) table->count++;

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool tableDelete(const Table *table, const Value key) {
    if (table->count == 0) return false;

    Entry *entry = findEntry(table->entries, table->capacity, key);
    if (entry->key.type == VAL_EMPTY) return false;

    entry->key = EMPTY_VAL;
    entry->value = EMPTY_VAL;
    return true;
}

void tableAddAll(const Table *from, Table *to) {
    for (int i = 0; i < from->capacity; i++) {
        const Entry *entry = &from->entries[i];
        if (entry->key.type != VAL_EMPTY) {
            tableSet(to, entry->key, entry->value);
        }
    }
}

ObjString *tableFindString(const Table *table, const char *chars, const int length, const uint32_t hash) {
    if (table->count == 0) return NULL;

    uint32_t index = hash % table->capacity;
    for (;;) {
        const Entry *entry = &table->entries[index];
        if (entry->key.type == VAL_EMPTY) {
            if (IS_NIL(entry->value)) return NULL;
        } else if (AS_STRING(entry->key)->length == length
                   && AS_STRING(entry->key)->hash == hash
                   && memcmp(AS_STRING(entry->key)->chars, chars, length) == 0) {
            return AS_STRING(entry->key);
        }

        index = (index + 1) % table->capacity;
    }
}

void markTable(Table *table) {
    for (int i = 0; i < table->capacity; i++) {
        Entry *entry = &table->entries[i];
        markValue(entry->key);
        markValue(entry->value);
    }
}

void tableRemoveWhiet(Table *table) {
    for (int i = 0; i < table->capacity; i++) {
        Entry *entry = &table->entries[i];
        if (entry->key.type != VAL_EMPTY && IS_OBJ(entry->key) && getMarkValue(AS_OBJ(entry->key)) != vm.markValue) {
            tableDelete(table, entry->key);
        }
    }
}
