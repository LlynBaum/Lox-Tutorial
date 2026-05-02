#ifndef COMPILER_C_GLOBAL_H
#define COMPILER_C_GLOBAL_H

#include "common.h"
#include "memory.h"
#include "table.h"

#define SET_GLOBAL(index, v) (vm.globals.values[index].value = v)

#define IS_IMMUTABLE_GLOBAL(index)  (vm.globals.values[index].immutable)

typedef struct {
    Value value;
    bool immutable;
} Global;

typedef struct {
    Table globalNames;
    int capacity;
    int count;
    Global *values;
} Globals;

void initGlobals(Globals *globals);

void freeGlobals(Globals *globals);

int declareGlobal(Globals *globals, const ObjString *name, bool immutable);

void defineGlobal(Globals *globals, const ObjString *name, Value value, bool immutable);

bool lookUpGlobal(const Globals *globals, const ObjString *name, int *out);

static inline bool getGlobal(const Globals globals, const int index, Value *out) {
    *out = globals.values[index].value;
    return !IS_UNDEFINED(*out);
}

static inline bool setGlobal(const Globals globals, const int index, const Value value) {
    if (IS_UNDEFINED(globals.values[index].value)) return false;
    globals.values[index].value = value;
    return true;
}

#endif //COMPILER_C_GLOBAL_H
