#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "chunk.h"
#include "common.h"
#include "vm.h"

#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "stdlib/cast.h"
#include "stdlib/joinStr.h"
#include "table.h"
#include "utils/stringUtils.h"
#include "stdlib/time.h"
#include "stdlib/nativeIo.h"
#include "stdlib/nativeErr.h"
#include "stdlib/classUtils.h"
#include "value.h"

#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

VM vm;

static void resetStack()
{
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

static void runtimeError(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm.frameCount - 1; i >= 0; i--)
    {
        const CallFrame *frame = &vm.frames[i];
        const ObjFunction *function = frame->closure->function;
        const size_t instruction = frame->ip - function->chunk.code - 1;
        const int line = getLine(&frame->closure->function->chunk, instruction);

        fprintf(stderr, "[line %d] in ", line);
        if (function->name == NULL)
        {
            fprintf(stderr, "script\n");
        }
        else
        {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    resetStack();
}

static void defineNative(const char *name, const NativeFn function)
{
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    defineGlobal(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1], true);
    popn(2);
}

void initVM()
{
    resetStack();
    vm.markValue = true;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;
    vm.objects = NULL;
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    initGlobals(&vm.globals);
    initTable(&vm.strings);

    vm.initString = OBJ_VAL(NULL); // GC might try to collect un init memory in copyString.
    vm.initString = OBJ_VAL(copyString("init", 4));

    defineNative("clock", clockNative);
    defineNative("read", readNative);
    defineNative("err", errNative);
    defineNative("str", strNative);
    defineNative("number", numberNative);
    defineNative("tryNumber", tryNumberNative);
    defineNative("bool", boolNative);
    defineNative("sleep", sleepNative);
    defineNative("joinStr", joinStrNative);
    defineNative("hasProperty", hasPropertyNative);
    defineNative("delProperty", delPropertyNative);
}

void freeVM()
{
    vm.initString = OBJ_VAL(NULL); // GC will free the string
    freeObjects();
    freeGlobals(&vm.globals);
    freeTable(&vm.strings);
}

void push(const Value value)
{
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}

Value popn(const int n)
{
    vm.stackTop -= n;
    return *vm.stackTop;
}

Value peek(const int distance)
{
    return vm.stackTop[-1 - distance];
}

void replace(const Value value)
{
    *(vm.stackTop - 1) = value;
}

static bool call(ObjClosure *closure, const uint8_t argCount)
{
    if (argCount != closure->function->arity)
    {
        runtimeError("Expected %d arguments but got %d", closure->function->arity, argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX)
    {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame *frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1;
    return true;
}

static bool callValue(const Value callee, const uint8_t argCount)
{
    if (IS_OBJ(callee))
    {
        switch (OBJ_TYPE(callee))
        {
            case OBj_BOUND_METHOD: {
                const ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                vm.stackTop[-argCount - 1] = bound->receiver;
                return call(bound->method, argCount);
            }
            case OBJ_CLASS: {
                ObjClass* klass = AS_CLASS(callee);
                vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
                Value initializer;
                if (tableGet(&klass->methods, vm.initString, &initializer)) {
                    return call(AS_CLOSURE(initializer), argCount);
                }
                if (argCount != 0) {
                    runtimeError("Expected 0 arguments but got %d", argCount);
                    return false;
                }
                return true;
            }
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                const NativeFn native = AS_NATIVE(callee);
                if (native(argCount, vm.stackTop - argCount))
                {
                    vm.stackTop -= argCount;
                    return true;
                }
                runtimeError(AS_STRING(vm.stackTop[-argCount - 1])->chars);
                return false;
            }
            default:
                break;
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
}

static bool bindMethod(const ObjClass* klass, const Value name) {
    Value method;
    if(!tableGet(&klass->methods, name, &method)){
        runtimeError("Undefined property '%s'.", AS_CSTRING(name));
        return false;
    }

    ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
    replace(OBJ_VAL(bound));
    return true;
}

static ObjUpvalue *captureUpvalue(Value *local)
{
    ObjUpvalue *prevUpvalue = NULL;
    ObjUpvalue *upvalue = vm.openUpvalues;
    while (upvalue != NULL && upvalue->location > local)
    {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local)
    {
        return upvalue;
    }

    ObjUpvalue *createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;
    if (prevUpvalue == NULL)
    {
        vm.openUpvalues = createdUpvalue;
    }
    else
    {
        prevUpvalue->next = createdUpvalue;
    }
    return createdUpvalue;
}

static void closeUpvalues(const Value *last)
{
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last)
    {
        ObjUpvalue *upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

static void defineMethod(const Value name) {
    const Value method = peek(0);
    ObjClass* klass = AS_CLASS(peek(1));
    tableSet(&klass->methods, name, method);
    pop();
}

static bool isFalsey(const Value value)
{
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

bool isTruthy(const Value v)
{
    if (IS_NIL(v))
        return false;
    if (IS_BOOL(v))
        return AS_BOOL(v);
    return true;
}

static void concatenate() {
    const ObjString *b = AS_STRING(peek(0));
    const ObjString *a = AS_STRING(peek(1));

    const ObjString *result = concatenateStrings(a->chars, a->length, b->chars, b->length);

    const Value resultVal = OBJ_VAL(result);
    tableSet(&vm.strings, resultVal, NIL_VAL);
    pop();
    replace(resultVal);
}

static bool numberToI64(const Value v, int64_t *out)
{
    if (!IS_NUMBER(v))
        return false;
    const double d = AS_NUMBER(v);
    const double t = trunc(d);
    if (d != t)
        return false;
    if (t < (double)INT64_MIN || t > (double)INT64_MAX)
        return false;

    *out = (int64_t)t;
    return true;
}

static InterpretResult run()
{
    CallFrame *frame = &vm.frames[vm.frameCount - 1];
    register uint8_t *ip = frame->ip;

#define READ_U8() (*ip++)
#define READ_U16() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define READ_U24() (ip += 3, (int)((ip[-3] << 16) | (uint16_t)((ip[-2] << 8) | ip[-1])))
#define READ_INDEX() (wideInstruction ? READ_U24() : READ_U8())
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_INDEX()])
#define BINARY_OP(valueType, op)                        \
    do                                                  \
    {                                                   \
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) \
        {                                               \
            frame->ip = ip;                             \
            runtimeError("Operands must be numbers.");  \
            return INTERPRET_RUNTIME_ERROR;             \
        }                                               \
        const double b = AS_NUMBER(pop());              \
        const double a = AS_NUMBER(peek(0));            \
        replace(valueType(a op b));                     \
    } while (false);

#define BIT_OP(op)                                                \
    do                                                            \
    {                                                             \
        int64_t a, b;                                             \
        if (!numberToI64(pop(), &b) || !numberToI64(peek(0), &a)) \
        {                                                         \
            frame->ip = ip;                                       \
            runtimeError("Operands must be numbers.");            \
            return INTERPRET_RUNTIME_ERROR;                       \
        }                                                         \
        const uint64_t result = a op(int) b;                      \
        replace(NUMBER_VAL((double)result));                      \
    } while (false);

    while (false)
        ;

    for (;;)
    {
#ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (const Value *slot = vm.stack; slot < vm.stackTop; slot++)
        {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        disassembleInstruction(&frame->closure->function->chunk, (int)(ip - frame->closure->function->chunk.code));
#endif // DEBUG_TRACE_EXECUTION

        uint8_t instruction = READ_U8();
        bool wideInstruction = false;

        if (instruction == OP_WIDE)
        {
            wideInstruction = true;
            instruction = READ_U8();
        }

        switch (instruction)
        {
        case OP_CONSTANT:
        {
            const Value constant = READ_CONSTANT();
            push(constant);
            break;
        }
        case OP_CONSTANT_M1:
            push(NUMBER_VAL(-1));
            break;
        case OP_CONSTANT_0:
            push(NUMBER_VAL(0));
            break;
        case OP_CONSTANT_1:
            push(NUMBER_VAL(1));
            break;
        case OP_CONSTANT_2:
            push(NUMBER_VAL(2));
            break;
        case OP_NIL:
            push(NIL_VAL);
            break;
        case OP_TRUE:
            push(BOOL_VAL(true));
            break;
        case OP_FALSE:
            push(BOOL_VAL(false));
            break;
        case OP_POP:
            pop();
            break;
        case OP_POPN:
        {
            const int popCount = READ_INDEX();
            popn(popCount);
            break;
        }
        case OP_DUP:
            push(peek(0));
            break;
        case OP_GET_LOCAL:
        {
            const int slot = READ_INDEX();
            push(frame->slots[slot]);
            break;
        }
        case OP_SET_LOCAL:
        {
            const int slot = READ_INDEX();
            frame->slots[slot] = peek(0);
            break;
        }
        case OP_INC_LOCAL:
        {
            const int slot = READ_INDEX();
            const int8_t imm = READ_U8();

            const Value value = frame->slots[slot];
            if (!IS_NUMBER(value))
            {
                frame->ip = ip;
                runtimeError("Operands must be a numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }

            const Value newValue = NUMBER_VAL(AS_NUMBER(value) + imm);
            frame->slots[slot] = newValue;
            push(newValue);
            break;
        }
        case OP_DEC_LOCAL:
        {
            const int slot = READ_INDEX();
            const int8_t imm = READ_U8();

            const Value value = frame->slots[slot];
            if (!IS_NUMBER(value))
            {
                frame->ip = ip;
                runtimeError("Operands must be a numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }

            const Value newValue = NUMBER_VAL(AS_NUMBER(value) - imm);
            frame->slots[slot] = newValue;
            push(newValue);
            break;
        }
        case OP_GET_GLOBAL:
        {
            const int index = READ_INDEX();
            Value value;
            if (!getGlobal(vm.globals, index, &value))
            {
                frame->ip = ip;
                runtimeError("Undefined variable.");
                return INTERPRET_RUNTIME_ERROR;
            }
            push(value);
            break;
        }
        case OP_DEFINE_GLOBAL:
        {
            const int index = READ_INDEX();
            SET_GLOBAL(index, pop());
            break;
        }
        case OP_SET_GLOBAL:
        {
            const int index = READ_INDEX();
            if (!setGlobal(vm.globals, index, peek(0)))
            {
                frame->ip = ip;
                runtimeError("Undefined variable.");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_GET_UPVALUE:
        {
            uint8_t slot = READ_U8();
            push(*frame->closure->upvalues[slot]->location);
            break;
        }
        case OP_SET_UPVALUE:
        {
            uint8_t slot = READ_U8();
            *frame->closure->upvalues[slot]->location = peek(0);
            break;
        }
        case OP_GET_PROPERTY: {
            if(!IS_INSTANCE(peek(0))){
                runtimeError("Only instances have properties.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjInstance* instance = AS_INSTANCE(peek(0));
            Value name = READ_CONSTANT();

            Value value;
            if(tableGet(&instance->fields, name, &value)) {
                replace(value);
                break;
            }

            if(!bindMethod(instance->klass, name)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            
            break;
        }
        case OP_SET_PROPERTY: {
            if(!IS_INSTANCE(peek(1))){
                runtimeError("Only instances have properties.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjInstance* instance = AS_INSTANCE(peek(1));
            tableSet(&instance->fields, READ_CONSTANT(), peek(0));
            Value value = pop();
            replace(value);
            break;
        }
        case OP_EQUAL:
        {
            const Value b = pop();
            const Value a = peek(0);
            replace(BOOL_VAL(valuesEqual(a, b)));
            break;
        }
        case OP_GREATER:
            BINARY_OP(BOOL_VAL, >);
            break;
        case OP_LESS:
            BINARY_OP(BOOL_VAL, <);
            break;
        case OP_ADD:
        {
            if (IS_STRING(peek(0)) && IS_STRING(peek(1)))
            {
                concatenate();
            }
            else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1)))
            {
                BINARY_OP(NUMBER_VAL, +);
            }
            else
            {
                frame->ip = ip;
                runtimeError("Operands must be two numbers or two strings.");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_SUBTRACT:
            BINARY_OP(NUMBER_VAL, -);
            break;
        case OP_MULTIPLY:
            BINARY_OP(NUMBER_VAL, *);
            break;
        case OP_DIVIDE:
            BINARY_OP(NUMBER_VAL, /);
            break;
        case OP_MOD:
        {
            if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1)))
            {
                frame->ip = ip;
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
            const double b = AS_NUMBER(pop());
            const double a = AS_NUMBER(peek(0));
            const double result = fmod(a, b);
            replace(NUMBER_VAL(result));
            break;
        }
        case OP_SHIFT_RIGHT:
            BIT_OP(>>);
            break;
        case OP_SHIFT_LEFT:
            BIT_OP(<<);
            break;
        case OP_BIT_AND:
            BIT_OP(&);
            break;
        case OP_BIT_OR:
            BIT_OP(|);
            break;
        case OP_BIT_XOR:
            BIT_OP(^);
            break;
        case OP_NOT:
        {
            replace(BOOL_VAL(isFalsey(peek(0))));
            break;
        }
        case OP_NEGATE:
        {
            if (!IS_NUMBER(peek(0)))
            {
                frame->ip = ip;
                runtimeError("Operand must be a number.");
                return INTERPRET_RUNTIME_ERROR;
            }

            replace(NUMBER_VAL(-AS_NUMBER(peek(0))));
            break;
        }
        case OP_JOIN_STR:
        {
            const uint8_t argCount = READ_U8();
            Value result = joinString(argCount, vm.stackTop - argCount);
            popn(argCount);
            push(result);
            break;
        }
        case OP_PRINT:
        {
            printValue(pop());
            printf("\n");
            break;
        }
        case OP_JUMP:
        {
            const uint16_t offset = READ_U16();
            ip += offset;
            break;
        }
        case OP_JUMP_IF_TRUE:
        {
            const uint16_t offset = READ_U16();
            if (isTruthy(peek(0)))
                ip += offset;
            break;
        }
        case OP_JUMP_IF_FALSE:
        {
            const uint16_t offset = READ_U16();
            if (isFalsey(peek(0)))
                ip += offset;
            break;
        }
        case OP_JUMP_IF_NOT_EQUAL:
        {
            const uint16_t offset = READ_U16();
            if (!valuesEqual(peek(0), peek(1)))
                ip += offset;
            break;
        }
        case OP_LOOP:
        {
            const uint16_t offset = READ_U16();
            ip -= offset;
            break;
        }
        case OP_LOOP_IF_FALSE:
        {
            const uint16_t offset = READ_U16();
            if (isFalsey(peek(0)))
                ip -= offset;
            break;
        }
        case OP_CALL:
        {
            const uint8_t argCount = READ_U8();
            frame->ip = ip;
            if (!callValue(peek(argCount), argCount))
            {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm.frames[vm.frameCount - 1];
            ip = frame->ip;
            break;
        }
        case OP_CLOSURE:
        {
            ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
            ObjClosure *closure = newClosure(function);
            push(OBJ_VAL(closure));
            for (int i = 0; i < closure->upvalueCount; ++i)
            {
                uint8_t isLocal = READ_U8();
                uint8_t index = READ_U8();
                if (isLocal)
                {
                    closure->upvalues[i] = captureUpvalue(frame->slots + index);
                }
                else
                {
                    closure->upvalues[i] = frame->closure->upvalues[index];
                }
            }
            break;
        }
        case OP_CLOSE_UPVALUE:
        {
            closeUpvalues(vm.stackTop - 1);
            pop();
            break;
        }
        case OP_RETURN:
        {
            const Value result = pop();
            closeUpvalues(frame->slots);
            vm.frameCount--;
            if (vm.frameCount == 0)
            {
                pop();
                return INTERPRET_OK;
            }

            vm.stackTop = frame->slots;
            push(result);
            frame = &vm.frames[vm.frameCount - 1];
            ip = frame->ip;
            break;
        }
        case OP_CLASS: 
            push(OBJ_VAL(newClass(AS_STRING(READ_CONSTANT()))));
            break;
        case OP_METHOD:
            defineMethod(READ_CONSTANT());
            break;
        default:
            break; // Unreachable
        }
    }

#undef BINARY_OP
#undef READ_CONSTANT
#undef READ_INDEX
#undef READ_U24
#undef READ_U16
#undef READ_U8
}

InterpretResult interpret(const char *source)
{
    ObjFunction *function = compile(source);
    if (function == NULL)
        return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL(function));
    ObjClosure *closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);

    return run();
}
