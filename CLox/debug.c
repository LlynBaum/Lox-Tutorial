#include <stdio.h>
#include "debug.h"
#include "chunk.h"
#include "object.h"

void disassembleChunk(const Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);

    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

static int simpleInstruction(const char* name, const int offset) {
    printf("%s\n", name);
    return offset + 1;
}

static int constantInstructionU8(const char * name, const Chunk * chunk, const int offset) {
    const uint8_t constant = chunk->code[offset + 1];
    printf("%-16s %4d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 2;
}

static int constantInstructionU24(const char * name, const Chunk * chunk, const int offset) {
    const int constant = disassembleU24Constant(chunk, offset);
    printf("%-16s %4d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 4;
}

static int indexInstructionU8(const char * name, const Chunk * chunk, const int offset) {
    const uint8_t constant = chunk->code[offset + 1];
    printf("%-16s %4d \n", name, constant);
    return offset + 2;
}

static int indexInstructionU24(const char * name, const Chunk * chunk, const int offset) {
    const int constant = disassembleU24Constant(chunk, offset);
    printf("%-16s %4d \n", name, constant);
    return offset + 4;
}

static int incrementInstructionU8(const char * name, const Chunk * chunk, const int offset) {
    const uint8_t constant = chunk->code[offset + 1];
    const uint8_t imm = chunk->code[offset + 2];
    printf("%-16s %4d %4d \n", name, constant, imm);
    return offset + 3;
}

static int incrementInstructionU24(const char * name, const Chunk * chunk, const int offset) {
    const int constant = disassembleU24Constant(chunk, offset);
    const uint8_t imm = chunk->code[offset + 2];
    printf("%-16s %4d %4d \n", name, constant, imm);
    return offset + 5;
}

static int jumpInstruction(const char* name, const int sign, const Chunk* chunk, const int offset) {
    const uint16_t jump = disassembleU16Constant(chunk, offset);
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

int invokeInstructionU8(const char* name, const Chunk * chunk, const int offset) {
    const uint8_t constant = chunk->code[offset + 1];
    const uint8_t argCount = chunk->code[offset + 2];
    printf("%-16s (%d args) %4d '", name, argCount, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 3;
}

int invokeInstructionU24(const char* name, const Chunk * chunk, const int offset) {
    const int constant = disassembleU24Constant(chunk, offset);
    const uint8_t argCount = chunk->code[offset + 2];
    printf("%-16s (%d args) %4d '", name, argCount, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 5;
}

int disassembleInstruction(const Chunk* chunk, int offset) {
#define constInstruction(nameU8, nameU24, chunk, offset) wideInstruction \
        ? constantInstructionU24(nameU24, chunk, offset) \
        : constantInstructionU8(nameU8, chunk, offset);

#define indexInstruction(nameU8, nameU24, chunk, offset) wideInstruction \
        ? indexInstructionU24(nameU24, chunk, offset) \
        : indexInstructionU8(nameU8, chunk, offset);

#define incrementInstruction(nameU8, nameU24, chunk, offset) wideInstruction \
        ? incrementInstructionU24(nameU24, chunk, offset) \
        : incrementInstructionU8(nameU8, chunk, offset);

#define invokeInstruction(nameU8, nameU24, chunk, offset) wideInstruction \
        ? invokeInstructionU24(nameU24, chunk, offset) \
        : invokeInstructionU8(nameU8, chunk, offset);

    printf("%04d ", offset);
    const int line = getLine(chunk, offset);
    if (offset > 0 && line == getLine(chunk, offset + 1)) {
        printf("   | ");
    } else {
        printf("%4d ", line);
    }

    uint8_t instruction = chunk->code[offset];
    bool wideInstruction = false;

    if (instruction == OP_WIDE) {
        wideInstruction = true;
        instruction = chunk->code[++offset];
    }

    switch (instruction) {
        case OP_WIDE:
            return simpleInstruction("OP_WIDE", offset);
        case OP_CONSTANT:
            return constInstruction("OP_CONSTANT", "OP_CONSTANT.W", chunk, offset);
        case OP_CONSTANT_M1:
            return simpleInstruction("OP_CONSTANT_M1", offset);
        case OP_CONSTANT_0:
            return simpleInstruction("OP_CONSTANT_0", offset);
        case OP_CONSTANT_1:
            return simpleInstruction("OP_CONSTANT_1", offset);
        case OP_CONSTANT_2:
            return simpleInstruction("OP_CONSTANT_2", offset);
        case OP_NIL:
            return simpleInstruction("OP_NIL", offset);
        case OP_TRUE:
            return simpleInstruction("OP_TRUE", offset);
        case OP_FALSE:
            return simpleInstruction("OP_FALSE", offset);
        case OP_POP:
            return simpleInstruction("OP_POP", offset);
        case OP_POPN:
            return indexInstruction("OP_POPN", "OP_POPN.W", chunk, offset)
        case OP_DUP:
            return simpleInstruction("OP_DUP", offset);
        case OP_GET_LOCAL:
            return indexInstruction("OP_GET_LOCAL", "OP_GET_LOCAL.W", chunk, offset);
        case OP_SET_LOCAL:
            return indexInstruction("OP_SET_LOCAL", "OP_SET_LOCAL.W", chunk, offset);
        case OP_INC_LOCAL:
            return incrementInstruction("OP_INC_LOCAL", "OP_INC_LOCAL.W", chunk, offset);
        case OP_DEC_LOCAL:
            return incrementInstruction("OP_DEC_LOCAL", "OP_DEC_LOCAL.W", chunk, offset);
        case OP_GET_GLOBAL:
            return indexInstruction("OP_GET_GLOBAL", "OP_GET_GLOBAL.W", chunk, offset);
        case OP_DEFINE_GLOBAL:
            return indexInstruction("OP_DEFINE_GLOBAL", "OP_DEFINE_GLOBAL.W", chunk, offset);
        case OP_SET_GLOBAL:
            return indexInstruction("OP_SET_GLOBAL", "OP_SET_GLOBAL.W", chunk, offset);
        case OP_GET_UPVALUE:
            return indexInstructionU8("OP_GET_UPVALUE", chunk, offset);
        case OP_SET_UPVALUE:
            return indexInstructionU8("OP_SET_UPVALUE", chunk, offset);
        case OP_GET_PROPERTY:
            return indexInstruction("OP_GET_PROPERTY", "OP_GET_PROPERTY.W", chunk, offset);
        case OP_SET_PROPERTY:
            return indexInstruction("OP_SET_PROPERTY", "OP_SET_PROPERTY.W", chunk, offset);
        case OP_GET_SUPER:
            return constInstruction("OP_GET_SUPER", "OP_GET_SUPER.W", chunk, offset);
        case OP_EQUAL:
            return simpleInstruction("OP_EQUAL", offset);
        case OP_GREATER:
            return simpleInstruction("OP_GREATER", offset);
        case OP_LESS:
            return simpleInstruction("OP_LESS", offset);
        case OP_ADD:
            return simpleInstruction("OP_ADD", offset);
        case OP_SUBTRACT:
            return simpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY:
            return simpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:
            return simpleInstruction("OP_DIVIDE", offset);
        case OP_MOD:
            return simpleInstruction("OP_MOD", offset);
        case OP_SHIFT_RIGHT:
            return simpleInstruction("OP_SHIFT_RIGHT", offset);
        case OP_SHIFT_LEFT:
            return simpleInstruction("OP_SHIFT_LEFT", offset);
        case OP_BIT_AND:
            return simpleInstruction("OP_BIT_AND", offset);
        case OP_BIT_OR:
            return simpleInstruction("OP_BIT_OR", offset);
        case OP_BIT_XOR:
            return simpleInstruction("OP_BIT_XOR", offset);
        case OP_NOT:
            return simpleInstruction("OP_NOT", offset);
        case OP_NEGATE:
            return simpleInstruction("OP_NEGATE", offset);
        case OP_JUMP:
            return jumpInstruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_TRUE:
            return jumpInstruction("OP_JUMP_IF_TRUE", 1, chunk, offset);
        case OP_JUMP_IF_FALSE:
            return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_JUMP_IF_NOT_EQUAL:
            return jumpInstruction("OP_JUMP_IF_NOT_EQUAL", 1, chunk, offset);
        case OP_LOOP:
            return jumpInstruction("OP_LOOP", -1, chunk, offset);
        case OP_LOOP_IF_FALSE:
            return jumpInstruction("OP_LOOP_IF_FALSE", -1, chunk, offset);
        case OP_JOIN_STR:
            return indexInstructionU8("OP_CALL", chunk, offset); 
        case OP_PRINT:
            return simpleInstruction("OP_PRINT", offset);
        case OP_CALL:
            return indexInstructionU8("OP_CALL", chunk, offset);
        case OP_CLOSURE: {
            const int constant = wideInstruction ? disassembleU24Constant(chunk, offset) : chunk->code[offset + 1];
            offset += wideInstruction ? 4 : 2;
            printf("%-16s %4d \n", wideInstruction ? "OP_CLOSURE.W" : "OP_CLOSURE", constant);

            const ObjFunction* function = AS_FUNCTION(chunk->constants.values[constant]);
            for (int i = 0; i < function->upvalueCount; ++i) {
                const int isLocal = chunk->code[offset++];
                const int index = chunk->code[offset++];
                printf("%04d      |                     %s %d\n",
                    offset - 2, isLocal ? "local" : "upvalue", index);
            }
            return offset;
        }
        case OP_INVOKE:
            return invokeInstruction("OP_INVOKE", "OP_INVOKE.W", chunk, offset);
        case OP_SUPER_INVOKE:
            return invokeInstruction("OP_SUPER_INVOKE", "OP_SUPER_INVOKE.W", chunk, offset);
        case OP_CLOSE_UPVALUE:
            return simpleInstruction("OP_CLOSE_UPVALUE", offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        case OP_CLASS:
            return constInstruction("OP_CLASS", "OP_CLASS.W", chunk, offset);
        case OP_INHERIT:
            return simpleInstruction("OP_INHERIT", offset);
        case OP_METHOD:
            return constInstruction("OP_METHOD", "OP_METHOD.W", chunk, offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }

#undef incrementInstruction
#undef indexInstruction
#undef constInstruction
}
