#include "stringUtils.h"
#include "../object.h"
#include "coerce.h"

#include <string.h>

Value joinString(const int argCount, Value *args) {
    int length = 0;
    for (int i = 0; i < argCount; i++)
    {
        if (!IS_STRING(args[i]))
        {
            args[i] = toString(args[i]);
        }

        length += AS_STRING(args[i])->length;
    }

    ObjString *result = allocateStringUnlinked(length);

    int current = 0;
    for (int i = 0; i < argCount; i++)
    {
        const ObjString *string = AS_STRING(args[i]);
        memcpy(result->chars + current, string->chars, string->length);
        current += string->length;
    }

    result->chars[length] = '\0';
    result->hash = hashString(result->chars, length);
    result = internString(result);

    if (nextObj((Obj*)result) == NULL) {
        linkObject((Obj*)result);
    }

    return OBJ_VAL(result);
}