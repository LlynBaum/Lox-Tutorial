#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"

#define OBJ_TYPE(value) (objType(AS_OBJ(value)))

#define IS_CLASS(value)    isObjType(value, OBJ_CLASS)
#define IS_CLOSURE(value)  isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value)   isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)   isObjType(value, OBJ_STRING)

#define AS_CLASS(value)    ((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value)  ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE(value)   (((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)   ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)  (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
    OBJ_CLASS,
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE
} ObjType;

struct Obj {
    uint64_t header;
};

struct ObjString {
    Obj obj;
    int length;
    uint32_t hash;
    char chars[];
};

typedef struct ObjUpvalue {
    Obj obj;
    Value* location;
    Value closed;
    struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk chunk;
    ObjString* name;
} ObjFunction;

typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalueCount;
} ObjClosure;

typedef struct {
    Obj obj;
    ObjString* name;
    Table methods;
} ObjClass;

typedef struct {
    Obj obj;
    ObjClass* klass;
    Table fields;
} ObjInstance;

typedef bool (*NativeFn)(int argCount, Value* args);

typedef struct {
    Obj obj;
    NativeFn function;
} ObjNative;

ObjClass* newClass(ObjString* name);
ObjClosure* newClosure(ObjFunction* function);
ObjFunction* newFunction();
ObjInstance* newInstance(ObjClass* klass);
ObjNative* newNative(NativeFn function);
ObjString* allocateString(int length);
uint32_t hashString(const char* key, int length);
ObjString* internString(ObjString* string);
ObjString* copyString(const char* chars, int length);
ObjString* concatenateStrings(const char* aChars, int aLength, const char* bChars, int bLength);
ObjUpvalue* newUpvalue(Value* slot);
void printObject(Value value);

static inline ObjType objType(Obj* object) {
    return (ObjType)((object->header >> 56) & 0xff);
}

static inline bool getMarkValue(Obj* object) {
    return (bool)((object->header >> 48) & 0x01);
}

static inline Obj* nextObj(Obj* object) {
    return (Obj*)(object->header & 0x0000ffffffffffff);
}

static inline void setIsMarked(Obj* object, bool isMarked) {
    object->header = (object->header & 0xff00ffffffffffff) | ((uint64_t)isMarked << 48);
}

static inline void setNextObj(Obj* object, Obj* next) {
    object->header = (object->header & 0xffff000000000000) | (uint64_t)next;
}

static inline bool isObjType(const Value value, const ObjType type) {
    return IS_OBJ(value) && objType(AS_OBJ(value)) == type;
}

#endif // clox_object_h
