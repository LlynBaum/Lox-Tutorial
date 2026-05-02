#include "chunk.h"

#include "memory.h"
#include "vm.h"

void initChunk(Chunk *chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lineCount = 0;
    chunk->lineCapacity = 0;
    chunk->lines = NULL;
    initValueArray(&chunk->constants);
}

void freeChunk(Chunk *chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(LineStart, chunk->lines, chunk->capacity);
    freeValueArray(&chunk->constants);
    initChunk(chunk);
}

void writeByte(Chunk *chunk, const uint8_t byte) {
    if (chunk->capacity < chunk->count + 1) {
        const int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
    }

    chunk->code[chunk->count++] = byte;
}

void writeChunk(Chunk *chunk, const uint8_t byte, const int line) {
    writeByte(chunk, byte);

    if (chunk->lineCount > 0 && chunk->lines[chunk->lineCount - 1].line == line) {
        return;
    }

    if (chunk->lineCapacity < chunk->lineCount + 1) {
        const int oldCapacity = chunk->lineCapacity;
        chunk->lineCapacity = GROW_CAPACITY(oldCapacity);
        chunk->lines = GROW_ARRAY(LineStart, chunk->lines, oldCapacity, chunk->lineCapacity);
    }

    LineStart *lineStart = &chunk->lines[chunk->lineCount++];
    lineStart->offset = chunk->count - 1;
    lineStart->line = line;
}

int addConstant(Chunk *chunk, const Value value) {
    push(value);
    writeValueArray(&chunk->constants, value);
    pop();
    return chunk->constants.count - 1;
}

bool writeIndexBytes(const OpCode code, Chunk *chunk, const int index) {
    if (index < 256) {
        writeByte(chunk, code);
        writeByte(chunk, index);
        return true;
    }
    if (index < 0xFFFFFF) {
        writeByte(chunk, OP_WIDE);
        writeByte(chunk, code);
        writeByte(chunk, (index >> 16) & 0xff);
        writeByte(chunk, (index >> 8) & 0xff);
        writeByte(chunk, index & 0xff);
        return true;
    }

    return false;
}

bool writeIndex(const OpCode code, Chunk *chunk, const int index, const int line) {
    if (index < 256) {
        writeChunk(chunk, code, line);
        writeChunk(chunk, index, line);
        return true;
    }
    if (index < 0xFFFFFF) {
        writeChunk(chunk, OP_WIDE, line);
        writeChunk(chunk, code, line);
        writeChunk(chunk, (index >> 16) & 0xff, line);
        writeChunk(chunk, (index >> 8) & 0xff, line);
        writeChunk(chunk, index & 0xff, line);
        return true;
    }

    return false;
}

bool writeConstantCode(const OpCode code, Chunk *chunk, const Value value, const int line) {
    const int index = addConstant(chunk, value);
    return writeIndex(code, chunk, index, line);
}

bool writeConstant(Chunk *chunk, const Value value, const int line) {
    return writeConstantCode(OP_CONSTANT, chunk, value, line);
}

int getLine(const Chunk *chunk, const size_t instruction) {
    int start = 0;
    int end = chunk->lineCount - 1;

    while (true) {
        const int mid = (start + end) / 2;
        const LineStart *line = &chunk->lines[mid];

        if (instruction < line->offset) {
            end = mid - 1;
        } else if (mid == chunk->lineCount - 1 || instruction < chunk->lines[mid + 1].offset) {
            return line->line;
        } else {
            start = mid + 1;
        }
    }
}
