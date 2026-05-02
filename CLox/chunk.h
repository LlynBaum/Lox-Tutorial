#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

typedef enum {
    OP_WIDE,
    OP_CONSTANT,
    OP_CONSTANT_M1,
    OP_CONSTANT_0,
    OP_CONSTANT_1,
    OP_CONSTANT_2,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_POPN,
    OP_DUP,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_INC_LOCAL,
    OP_DEC_LOCAL,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_SET_PROPERTY,
    OP_GET_PROPERTY,
    OP_GET_SUPER,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MOD,
    OP_SHIFT_RIGHT,
    OP_SHIFT_LEFT,
    OP_BIT_AND,
    OP_BIT_OR,
    OP_BIT_XOR,
    OP_NOT,
    OP_NEGATE,
    OP_JOIN_STR,
    OP_PRINT,
    OP_JUMP,
    OP_JUMP_IF_TRUE,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_NOT_EQUAL,
    OP_LOOP,
    OP_LOOP_IF_FALSE,
    OP_CALL,
    OP_INVOKE,
    OP_SUPER_INVOKE,
    OP_CLOSURE,
    OP_CLOSE_UPVALUE,
    OP_RETURN,
    OP_CLASS,
    OP_INHERIT,
    OP_METHOD
} OpCode;

typedef struct {
    int offset;
    int line;
} LineStart;

typedef struct {
    int count;
    int capacity;
    uint8_t *code;
    ValueArray constants;
    int lineCount;
    int lineCapacity;
    LineStart *lines;
} Chunk;

void initChunk(Chunk *chunk);

void freeChunk(Chunk *chunk);

void writeChunk(Chunk *chunk, uint8_t byte, int line);

int addConstant(Chunk *chunk, Value value);

bool writeIndexBytes(OpCode code, Chunk *chunk, int index);

bool writeIndex(OpCode code, Chunk *chunk, int index, int line);

bool writeConstantCode(OpCode code, Chunk *chunk, Value value, int line);

bool writeConstant(Chunk *chunk, Value value, int line);

int getLine(const Chunk *chunk, size_t instruction);

#endif // clox_chunk_h
