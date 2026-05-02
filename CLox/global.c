#include "global.h"

void initGlobals(Globals *globals) {
    initTable(&globals->globalNames);
    globals->count = 0;
    globals->capacity = 0;
    globals->values = NULL;
}

void freeGlobals(Globals *globals) {
    freeTable(&globals->globalNames);
    FREE_ARRAY(Value, globals->values, globals->capacity);
    globals->count = 0;
    globals->capacity = 0;
    globals->values = NULL;
}

int declareGlobal(Globals *globals, const ObjString *name, const bool immutable) {
    const int newIndex = globals->count;

    if (globals->capacity < globals->count + 1) {
        const int oldCapacity = globals->capacity;
        globals->capacity = GROW_CAPACITY(oldCapacity);
        globals->values = GROW_ARRAY(Global, globals->values, oldCapacity, globals->capacity);
    }

    Global *global = &globals->values[globals->count++];
    global->value = UNDEFINED_VAL;
    global->immutable = immutable;

    tableSet(&globals->globalNames, OBJ_VAL(name), NUMBER_VAL((double)newIndex));
    return newIndex;
}

void defineGlobal(Globals *globals, const ObjString *name, const Value value, const bool immutable) {
    const int index = declareGlobal(globals, name, immutable);
    globals->values[index].value = value;
}

bool lookUpGlobal(const Globals *globals, const ObjString *name, int *out) {
    Value index;
    if (tableGet(&globals->globalNames, OBJ_VAL(name), &index)) {
        *out = AS_NUMBER(index);
        return true;
    }
    return false;
}
