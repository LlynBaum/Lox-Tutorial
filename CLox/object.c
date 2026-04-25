#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(const size_t size, const ObjType type) {
    Obj* obj = reallocate(NULL, 0, size);
    obj->header = (uint64_t)vm.objects | (uint64_t)vm.markValue << 48 | (uint64_t)type << 56;
    vm.objects = obj;

#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*)obj, size, type);
#endif // DEBUG_LOG_GC

    return obj;
}

ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method) {
    ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod, OBj_BOUND_METHOD);

    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

ObjClass* newClass(ObjString* name) {
    ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
    klass->name = name;
    initTable(&klass->methods);
    return klass;
}

ObjClosure* newClosure(ObjFunction* function)
{
    ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; ++i)
    {
        upvalues[i] = NULL;
    }

    ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

ObjFunction* newFunction()
{
    ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

ObjInstance* newInstance(ObjClass* klass) {
    ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
    instance->klass = klass;
    initTable(&instance->fields);
    return instance;
}

ObjNative* newNative(const NativeFn function)
{
    ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    return native;
}

ObjString* allocateString(const int length)
{
    ObjString* string = (ObjString*)allocateObject(sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = length;
    return string;
}

uint32_t hashString(const char* key, const int length)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++)
    {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

ObjString* copyString(const char* chars, const int length)
{
    const uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);

    if (interned != NULL) return interned;

    ObjString* string = allocateString(length);
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    string->hash = hash;
    push(OBJ_VAL(string));
    tableSet(&vm.strings, OBJ_VAL(string), NIL_VAL);
    pop();
    return string;
}

ObjString* internString(ObjString* string)
{
    ObjString* interned = tableFindString(&vm.strings, string->chars, string->length, string->hash);

    if (interned != NULL)
    {
        reallocate(string, sizeof(ObjString) + string->length + 1, 0);
        return interned;
    }

    return string;
}

ObjString* concatenateStrings(const char* aChars, const int aLength, const char* bChars, const int bLength)
{
    const int length = aLength + bLength;
    ObjString* result = allocateString(length);
    memcpy(result->chars, aChars, aLength);
    memcpy(result->chars + aLength, bChars, bLength);
    result->chars[length] = '\0';
    result->hash = hashString(result->chars, length);

    push(OBJ_VAL(result));
    result = internString(result);
    pop();
    return result;
}

ObjUpvalue* newUpvalue(Value* slot)
{
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
    upvalue->next = NULL;
    return upvalue;
}

static void printFunction(const ObjFunction* function)
{
    if (function->name == NULL)
    {
        printf("<script>");
        return;
    }

    printf("<fn %s>", function->name->chars);
}

void printObject(const Value value)
{
    switch (OBJ_TYPE(value))
    {
        case OBj_BOUND_METHOD:
            printFunction(AS_BOUND_METHOD(value)->method->function);
            break;
        case OBJ_CLASS:
            printf("%s", AS_CLASS(value)->name->chars);
            break;
        case OBJ_CLOSURE:
            printFunction(AS_CLOSURE(value)->function);
            break;
        case OBJ_FUNCTION:
            printFunction(AS_FUNCTION(value));
            break;
        case OBJ_INSTANCE:
            printf("%s instance", AS_INSTANCE(value)->klass->name->chars);
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_UPVALUE:
            printf("upvalue");
            break;
    }
}