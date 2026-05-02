#ifndef clox_compiler_h
#define clox_compiler_h

#include "object.h"

#define MAX_LOOP_DEPTH 64

typedef enum { BINDING_LOCAL, BINDING_UPVALUE, BINDING_GLOBAL } BindingKind;

ObjFunction *compile(const char *source);

void markCompilerRoots();

#endif //clox_compiler_h
