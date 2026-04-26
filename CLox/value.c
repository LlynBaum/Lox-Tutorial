#include "value.h"

#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"

void initValueArray(ValueArray *array)
{
    array->count = 0;
    array->capacity = 0;
    array->values = NULL;
}

void freeValueArray(ValueArray *array)
{
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}
void writeValueArray(ValueArray *array, const Value value)
{
    if (array->capacity < array->count + 1)
    {
        const int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

bool valuesEqual(const Value a, const Value b)
{
    if (a.type != b.type)
        return false;
    switch (a.type)
    {
    case VAL_BOOL:
        return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL:
        return true;
    case VAL_NUMBER:
        return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ: {
        if (objType((AS_OBJ(a))) == OBJ_STRING && objType((AS_OBJ(b))) == OBJ_STRING){
            const ObjString* aString = AS_STRING(a);
            const ObjString* bString = AS_STRING(b);
            return aString->length == bString->length &&
                memcmp(aString->chars, bString->chars,
                        aString->length) == 0;
        }
        return AS_OBJ(a) == AS_OBJ(b);
    }
    default:
        return false;
    }
}

void printValue(const Value value)
{
    switch (value.type)
    {
    case VAL_BOOL:
        printf(AS_BOOL(value) ? "true" : "false");
        break;
    case VAL_NIL:
        printf("nil");
        break;
    case VAL_NUMBER:
        printf("%.17g", AS_NUMBER(value));
        break;
    case VAL_OBJ:
        printObject(value);
        break;
    case VAL_UNDEFINED:
        printf("undefined");
        break;
    case VAL_EMPTY:
        printf("empty");
        break;
    }
}