#include "compiler.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "memory.h"
#include "scanner.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif // DEBUG_PRINT_CODE

typedef struct
{
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum
{
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_BIT_OR,
    PREC_BIT_XOR,
    PREC_BIT_AND,
    PREC_SHIFT,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY
} Precedence;

typedef enum
{
    FLOW_LOOP,
    FLOW_IF,
    FLOW_SWITCH,
    FLOW_SWITCH_EXPRESSION
} FlowKind;

typedef void (*ParseFn)(bool canAssign);

typedef struct
{
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct BreakPatch
{
    int jumpOffset;
    struct BreakPatch *next;
} JumpPatch;

typedef struct
{
    FlowKind kind;
    int innermostLoopStart;
    int innermostScopeDepth;
    JumpPatch *breakPatchHead;
} ControlFlowContext;

typedef struct
{
    Token name;
    int depth;
    bool immutable;
    bool isCaptured;
} Local;

typedef struct
{
    int index;
    bool isLocal;
} Upvalue;

typedef enum
{
    TYPE_FUNCTION,
    TYPE_ANONYMOUS_FUNCTION,
    TYPE_SCRIPT
} FunctionType;

typedef struct Compiler
{
    struct Compiler *enclosing;
    ObjFunction *function;
    FunctionType type;
    int anonymousFunctionCount;

    Local locals[UINT8_COUNT];
    int localCapacity;
    int localCount;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth;

    int controlFlowTop;
    ControlFlowContext controlFlowStack[MAX_LOOP_DEPTH];
} Compiler;

Parser parser;
Compiler *current = NULL;

static Chunk *currentChunk()
{
    return &current->function->chunk;
}

static ControlFlowContext *currentControlFlow()
{
    return current->controlFlowTop >= 0 ? &current->controlFlowStack[current->controlFlowTop] : NULL;
}

static void errorAt(const Token *token, const char *message)
{
    if (parser.panicMode)
        return;
    parser.panicMode = true;

    fprintf(stderr, "[line %d] Error", token->line);
    if (token->type == TOKEN_EOF)
    {
        fprintf(stderr, " at end");
    }
    else if (token->type == TOKEN_ERROR)
    {
    }
    else
    {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char *message)
{
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char *message)
{
    errorAt(&parser.current, message);
}

static bool errorIfImmutable(const Token *name, const BindingKind kind, const int index)
{
    switch (kind)
    {
    case BINDING_LOCAL:
        if (current->locals[index].immutable)
        {
            errorAt(name, "Can not assign value to a constant variable.");
            return true;
        }
        break;
    case BINDING_UPVALUE:
    { // TODO: test this shit xD
        const Compiler *compiler = current;
        const Upvalue *upvalue = &compiler->upvalues[index];
        while (!upvalue->isLocal)
        {
            upvalue = &compiler->upvalues[upvalue->index];
            compiler = compiler->enclosing;
        }

        if (compiler->locals[upvalue->index].immutable)
        {
            errorAt(name, "Can not assign value to a constant variable.");
            return true;
        }
    }
    case BINDING_GLOBAL:
        if (IS_IMMUTABLE_GLOBAL(index))
        {
            errorAt(name, "Can not assign value to a constant variable.");
            return true;
        }
        break;
    }
    return false;
}

static void advance()
{
    parser.previous = parser.current;

    for (;;)
    {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR)
            break;

        errorAtCurrent(parser.current.start);
    }
}

static void consume(const TokenType type, const char *message)
{
    if (parser.current.type == type)
    {
        advance();
        return;
    }

    errorAtCurrent(message);
}

static bool check(const TokenType type)
{
    return parser.current.type == type;
}

static bool match(const TokenType type)
{
    if (!check(type))
        return false;
    advance();
    return true;
}

static void emitByte(const uint8_t byte)
{
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(const uint8_t byte1, const uint8_t byte2)
{
    emitByte(byte1);
    emitByte(byte2);
}

static void emitLoop(const OpCode loopOp, const int loopStart)
{
    emitByte(loopOp);

    const int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX)
        error("Loop body too large.");

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

static int emitJump(const uint8_t instruction)
{
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void emitReturn()
{
    emitBytes(OP_NIL, OP_RETURN);
}

static void emitPopN(const int n)
{
    writeIndexBytes(OP_POPN, currentChunk(), n);
}

static void emitPop(const int popCount)
{
    if (popCount == 1)
    {
        emitByte(OP_POP);
    }
    else if (popCount > 1)
    {
        emitPopN(popCount);
    }
}

static void emitPopTo(const int targetDepth)
{
    int popCount = 0;
    for (int i = current->localCount - 1; i >= 0; i--)
    {
        if (current->locals[i].depth <= targetDepth)
        {
            break;
        }
        popCount++;
    }
    emitPop(popCount);
}

static int makeConstant(const Value value) {
    const int index = addConstant(currentChunk(), value);
    if (index > UINT24_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return index;
}

static int makeIdentifier(const Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start,
                                         name->length)));
}

static void emitConstant(const Value value) {
    const bool result = writeConstant(currentChunk(), value, parser.previous.line);
    if (!result) error("Too many constants in one chunk.");
}

static void emitClosure(const ObjFunction *closure) {
    const bool result = writeConstantCode(OP_CLOSURE, currentChunk(), OBJ_VAL(closure), parser.previous.line);
    if (!result) error("Too many constants in one chunk.");
}

static void emitIndex(const OpCode code, const int index, const int line) {
    const bool result = writeIndex(code, currentChunk(), index, line);
    if (!result) error("Too many identifier in one chunk.");
}

static void patchJump(const int offset)
{
    // -2 to adjust for the bytecode for the jump offset itself.
    const int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX)
    {
        error("Too much code to jump over.");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void initCompiler(Compiler *compiler, const FunctionType type, ObjString *name)
{
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->anonymousFunctionCount = 0;

    compiler->localCapacity = 1;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;

    compiler->controlFlowTop = -1;

    compiler->function = newFunction();
    current = compiler;
    current->function->name = name;

    Local *local = &current->locals[current->localCount++];
    local->depth = 0;
    local->immutable = true;
    local->isCaptured = false;
}

static ObjFunction *endCompiler()
{
    emitReturn();
    ObjFunction *function = current->function;

#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError)
    {
        disassembleChunk(currentChunk(), function->name != NULL
                                             ? function->name->chars
                                             : "<script>");
    }
#endif // DEBUG_PRINT_CODE

    current = current->enclosing;
    return function;
}

static void beginScope()
{
    if (current->scopeDepth + 1 >= STACK_MAX)
    {
        error("Too many scopes.");
    }

    current->scopeDepth++;
}

static void endScope()
{
    current->scopeDepth--;

    int popCount = 0;

    while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth)
    {
        if (current->locals[--current->localCount].isCaptured)
        {
            if (popCount > 0)
            {
                emitPop(popCount);
                popCount = 0;
            }
            emitByte(OP_CLOSE_UPVALUE);
        }
        else
        {
            popCount++;
        }
    }
    if (popCount > 0)
    {
        emitPop(popCount);
    }
}

static ControlFlowContext* enterControlFlow(const FlowKind kind)
{
    if (current->controlFlowTop + 1 == MAX_LOOP_DEPTH)
    {
        error("Too many nested Loops.");
        return NULL;
    }

    ControlFlowContext *ctx = &current->controlFlowStack[++current->controlFlowTop];
    ctx->kind = kind;
    ctx->innermostLoopStart = currentChunk()->count;
    ctx->innermostScopeDepth = current->scopeDepth;
    ctx->breakPatchHead = NULL;
    return ctx;
}

static void exitControlFlow()
{
    const ControlFlowContext *ctx = &current->controlFlowStack[current->controlFlowTop--];

    JumpPatch *patch = ctx->breakPatchHead;
    while (patch != NULL)
    {
        patchJump(patch->jumpOffset);
        JumpPatch *old = patch;
        patch = patch->next;
        free(old);
    }
}

static void expression();
static void statement();
static void declaration();
static void function(FunctionType type, ObjString *name);
static ParseRule *getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static int identifierConstant(const Token *name, const bool isAssignment, const bool immutable) {
    const ObjString* nameStr = copyString(name->start, name->length);

    int index;
    if (lookUpGlobal(&vm.globals, nameStr, &index))
    {
        return index;
    }

    if (!isAssignment)
    {
        errorAt(name, "Use of undeclared variable.");
    }

    return declareGlobal(&vm.globals, nameStr, immutable);
}

static bool identifiersEqual(const Token* a, const Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(const Compiler *compiler, const Token *name) {
    for (int i = compiler->localCount - 1; i >= 0; i--)
    {
        const Local *local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name))
        {
            if (local->depth == -1)
            {
                error("Can't read local variable in it's own initializer.");
            }
            return i;
        }
    }
    return -1;
}

static uint8_t addUpvalue(Compiler *compiler, const int index, const bool isLocal) {
    const int upvalueCount = compiler->function->upvalueCount;

    for (int i = 0; i < upvalueCount; ++i)
    {
        const Upvalue *upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal)
        {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT)
    {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler *compiler, const Token *name) {
    if (compiler->enclosing == NULL)
        return -1;

    const int local = resolveLocal(compiler->enclosing, name);
    if (local != -1)
    {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, local, true);
    }

    const int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1)
    {
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

static void addLocal(const Token name, const bool immutable) {
    if (current->localCount + 1 > UINT8_COUNT)
    {
        error("Too many local variables in function");
        return;
    }

    Local *local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->immutable = immutable;
    local->isCaptured = false;
}

static void declareVariable(const bool immutable) {
    if (current->scopeDepth == 0) return;

    const Token *name = &parser.previous;

    for (int i = current->localCount - 1; i >= 0; i--)
    {
        const Local *local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth)
        {
            break;
        }

        if (identifiersEqual(name, &local->name))
        {
            error("Already a variable with this name in this scope.");
        }
    }

    addLocal(*name, immutable);
}

static int parseVariable(const char *errorMessage, const bool immutable) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable(immutable);
    if (current->scopeDepth > 0) return 0;
    return identifierConstant(&parser.previous, true, immutable);
}

static void makeInitialized() {
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(const int index, const int line) {
    if (current->scopeDepth > 0)
    {
        makeInitialized();
        return;
    }
    emitIndex(OP_DEFINE_GLOBAL, index, line);
}

static uint8_t argumentList() {
    uint8_t argCount = 0;

    if (!check(TOKEN_RIGHT_PAREN))
    {
        do
        {
            expression();
            if (argCount == 255)
            {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

static void and_(bool _) {
    const int endJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    patchJump(endJump);
}

static void or_(bool _) {
    const int endJump = emitJump(OP_JUMP_IF_TRUE);

    emitByte(OP_POP);
    parsePrecedence(PREC_OR);

    patchJump(endJump);
}

static void binary(bool _) {
    const TokenType operatorType = parser.previous.type;
    const ParseRule *rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType)
    {
    case TOKEN_BANG_EQUAL:
        emitBytes(OP_EQUAL, OP_NOT);
        break;
    case TOKEN_EQUAL_EQUAL:
        emitByte(OP_EQUAL);
        break;
    case TOKEN_GREATER:
        emitByte(OP_GREATER);
        break;
    case TOKEN_GREATER_GREATER:
        emitByte(OP_SHIFT_RIGHT);
        break;
    case TOKEN_GREATER_EQUAL:
        emitBytes(OP_LESS, OP_NOT);
        break;
    case TOKEN_LESS:
        emitByte(OP_LESS);
        break;
    case TOKEN_LESS_LESS:
        emitByte(OP_SHIFT_LEFT);
        break;
    case TOKEN_LESS_EQUAL:
        emitBytes(OP_GREATER, OP_NOT);
        break;
    case TOKEN_PLUS:
        emitByte(OP_ADD);
        break;
    case TOKEN_MINUS:
        emitByte(OP_SUBTRACT);
        break;
    case TOKEN_STAR:
        emitByte(OP_MULTIPLY);
        break;
    case TOKEN_SLASH:
        emitByte(OP_DIVIDE);
        break;
    case TOKEN_PERCENT:
        emitByte(OP_MOD);
        break;
    case TOKEN_AND_OPERATOR:
        emitByte(OP_BIT_AND);
        break;
    case TOKEN_VERTICAL_BAR:
        emitByte(OP_BIT_OR);
        break;
    case TOKEN_CARET:
        emitByte(OP_BIT_XOR);
        break;
    default:
        return; // Unreachable
    }
}

static ObjString *makeAnonymousName(const int id, const int line) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "anonymous#%d@%d", id, line);
    if (n < 0)
        n = 0;
    if (n >= (int)sizeof(buf))
        n = (int)sizeof(buf) - 1;
    return copyString(buf, n);
}

static void anonymousFunction(bool _) {
    const int line = parser.previous.line;
    ObjString *debugName = makeAnonymousName(++current->anonymousFunctionCount, line);
    function(TYPE_ANONYMOUS_FUNCTION, debugName);
}

static void switchExpression(bool _) {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'switch'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' for switch body.");

    ControlFlowContext *ctx = enterControlFlow(FLOW_SWITCH_EXPRESSION);

    int caseExit = -1;
    while (match(TOKEN_CASE))
    {
        if (caseExit != -1)
        {
            patchJump(caseExit);
            emitByte(OP_POP);
        }
        
        expression();
        consume(TOKEN_COLON, "Expect ':' after condition.");

        caseExit = emitJump(OP_JUMP_IF_NOT_EQUAL);
        emitPopN(2);
        expression();
        consume(TOKEN_COMMA, "Expect ',' after case expression.");

        const int offset = emitJump(OP_JUMP);
        JumpPatch *patch = malloc(sizeof(JumpPatch));
        patch->jumpOffset = offset;
        patch->next = ctx->breakPatchHead;
        ctx->breakPatchHead = patch;
    }

    if (match(TOKEN_EOF))
    {
        error("Expect closing '}' after switch body.");
        return;
    }

    if (caseExit != -1)
    {
        patchJump(caseExit);
        emitPopN(2);
    }

    if (match(TOKEN_DEFAULT))
    {
        consume(TOKEN_COLON, "Expect ':' after 'default'.");
        expression();
        match(TOKEN_COMMA);
    }

    consume(TOKEN_RIGHT_BRACE, "Expect closing '}' after switch body.");
    exitControlFlow();
}

static void call(bool _) {
    const uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

static void dot(bool canAssign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    const int name = makeIdentifier(&parser.previous);

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(OP_SET_PROPERTY, name);
    } else {
        emitBytes(OP_GET_PROPERTY, name);
    }
}

static void literal(bool _) {
    switch (parser.previous.type)
    {
    case TOKEN_FALSE:
        emitByte(OP_FALSE);
        break;
    case TOKEN_TRUE:
        emitByte(OP_TRUE);
        break;
    case TOKEN_NIL:
        emitByte(OP_NIL);
        break;
    default:
        break; // Unreachable
    }
}

static void grouping(bool _)
{
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool _)
{
    const double value = strtod(parser.previous.start, NULL);
    if (value == -1)
    {
        emitByte(OP_CONSTANT_M1);
    }
    else if (value == 0)
    {
        emitByte(OP_CONSTANT_0);
    }
    else if (value == 1)
    {
        emitByte(OP_CONSTANT_1);
    }
    else if (value == 2)
    {
        emitByte(OP_CONSTANT_2);
    }
    else
    {
        emitConstant(NUMBER_VAL(value));
    }
}

static void string(bool _)
{
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static void postIncrementVariable(const int index, const BindingKind kind, const bool decrement, const int line)
{
    if (kind == BINDING_LOCAL)
    {
        emitIndex(decrement ? OP_DEC_LOCAL : OP_INC_LOCAL, index, line);
        emitByte(1);
    }
    else
    {
        emitByte(OP_DUP);
        emitBytes(OP_CONSTANT_1, decrement ? OP_SUBTRACT : OP_ADD);
        emitIndex(OP_SET_GLOBAL, index, line);
    }
    emitByte(OP_POP);
}

static void namedVariable(const Token name, const bool canAssign)
{
#define SELF_ASSIGN(op)                         \
    do                                          \
    {                                           \
        if (errorIfImmutable(&name, kind, arg)) \
            return;                             \
        advance();                              \
        expression();                           \
        emitIndex(getOp, arg, name.line);       \
        emitByte(op);                           \
        emitIndex(setOp, arg, name.line);       \
    } while (false);

    uint8_t getOp, setOp;
    BindingKind kind;
    int arg = resolveLocal(current, &name);
    if (arg != -1)
    {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
        kind = BINDING_LOCAL;
    }
    else if ((arg = resolveUpvalue(current, &name)) != -1)
    {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
        kind = BINDING_UPVALUE;
    }
    else
    {
        arg = identifierConstant(&name, false, false);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
        kind = BINDING_GLOBAL;
    }

    if (canAssign)
    {
        switch (parser.current.type)
        {
        case TOKEN_EQUAL:
        {
            if (errorIfImmutable(&name, kind, arg))
                return;
            advance();
            expression();
            emitIndex(setOp, arg, name.line);
            return;
        }
        case TOKEN_PLUS_EQUAL:
        {
            SELF_ASSIGN(OP_ADD)
            return;
        }
        case TOKEN_MINUS_EQUAL:
        {
            SELF_ASSIGN(OP_SUBTRACT)
            return;
        }
        case TOKEN_STAR_EQUAL:
        {
            SELF_ASSIGN(OP_MULTIPLY)
            return;
        }
        case TOKEN_SLASH_EQUAL:
        {
            SELF_ASSIGN(OP_DIVIDE)
            return;
        }
        default:
            break;
        }
    }

    emitIndex(getOp, arg, name.line);

    if (match(TOKEN_PLUS_PLUS) || match(TOKEN_MINUS_MINUS))
    {
        if (errorIfImmutable(&name, kind, arg))
            return;
        const bool decrement = parser.previous.type == TOKEN_MINUS_MINUS;
        postIncrementVariable(arg, kind, decrement, name.line);
    }
#undef SELF_ASSIGN
}

static void variable(const bool canAssign)
{
    namedVariable(parser.previous, canAssign);
}

static void preIncrementVariable(const bool _)
{
    int8_t opLocal, opGlobal;
    BindingKind kind;
    if (parser.previous.type == TOKEN_MINUS_MINUS)
    {
        opLocal = OP_DEC_LOCAL;
        opGlobal = OP_SUBTRACT;
        kind = BINDING_LOCAL;
    }
    else
    {
        opLocal = OP_INC_LOCAL;
        opGlobal = OP_ADD;
        kind = BINDING_GLOBAL;
    }

    consume(TOKEN_IDENTIFIER, "Expected variable name.");
    const Token name = parser.previous;
    int arg = resolveLocal(current, &name);
    if (errorIfImmutable(&name, kind, arg))
        return;

    if (arg != -1)
    {
        emitIndex(opLocal, arg, name.line);
        emitByte(1);
    }
    else
    {
        arg = identifierConstant(&name, false, false);
        emitIndex(OP_GET_GLOBAL, arg, name.line);
        emitBytes(OP_CONSTANT_1, opGlobal);
        emitIndex(OP_SET_GLOBAL, arg, name.line);
    }
}

static void interpolation(bool _) // TODO: test
{
    int args = 0;
    do
    {
        string(false);
        expression();
        args += 2;
    } while (match(TOKEN_INTERPOLATION));

    consume(TOKEN_STRING, "Expect end of string interpolation.");
    string(false);
    args++;

    if (args > UINT8_MAX)
    {
        error("Too many expressions in string interpolation");
    }

    emitBytes(OP_JOIN_STR, args);
}

static void unary(bool _)
{
    const TokenType operatorType = parser.previous.type;

    parsePrecedence(PREC_UNARY);

    switch (operatorType)
    {
    case TOKEN_BANG:
        emitByte(OP_NOT);
        break;
    case TOKEN_MINUS:
        emitByte(OP_NEGATE);
        break;
    default:
        return; // Unreachable
    }
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]       = {grouping,             call,    PREC_CALL     },
    [TOKEN_RIGHT_PAREN]      = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_LEFT_BRACE]       = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_RIGHT_BRACE]      = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_COMMA]            = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_DOT]              = {NULL,                 dot,     PREC_CALL     },
    [TOKEN_MINUS]            = {unary,                binary,  PREC_TERM     },
    [TOKEN_MINUS_MINUS]      = {preIncrementVariable, NULL,    PREC_UNARY    },
    [TOKEN_PLUS]             = {NULL,                 binary,  PREC_TERM     },
    [TOKEN_PLUS_PLUS]        = {preIncrementVariable, NULL,    PREC_UNARY    },
    [TOKEN_SEMICOLON]        = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_SLASH]            = {NULL,                 binary,  PREC_FACTOR   },
    [TOKEN_PERCENT]          = {NULL,                 binary,  PREC_FACTOR   },
    [TOKEN_STAR]             = {NULL,                 binary,  PREC_FACTOR   },
    [TOKEN_BANG]             = {unary,                NULL,    PREC_NONE     },
    [TOKEN_BANG_EQUAL]       = {NULL,                 binary,  PREC_EQUALITY },
    [TOKEN_EQUAL]            = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_EQUAL_EQUAL]      = {NULL,                 binary,  PREC_EQUALITY },
    [TOKEN_GREATER]          = {NULL,                 binary,  PREC_EQUALITY },
    [TOKEN_GREATER_GREATER]  = {NULL,                 binary,  PREC_SHIFT    },
    [TOKEN_GREATER_EQUAL]    = {NULL,                 binary,  PREC_EQUALITY },
    [TOKEN_LESS]             = {NULL,                 binary,  PREC_EQUALITY },
    [TOKEN_LESS_LESS]        = {NULL,                 binary,  PREC_SHIFT    },
    [TOKEN_LESS_EQUAL]       = {NULL,                 binary,  PREC_EQUALITY },
    [TOKEN_AND_OPERATOR]     = {NULL,                 binary,  PREC_BIT_AND  },
    [TOKEN_VERTICAL_BAR]     = {NULL,                 binary,  PREC_BIT_OR   },
    [TOKEN_CARET]            = {NULL,                 binary,  PREC_BIT_XOR  },
    [TOKEN_IDENTIFIER]       = {variable,             NULL,    PREC_NONE     },
    [TOKEN_STRING]           = {string,               NULL,    PREC_NONE     },
    [TOKEN_INTERPOLATION]    = {interpolation,        NULL,    PREC_NONE     },
    [TOKEN_NUMBER]           = {number,               NULL,    PREC_NONE     },
    [TOKEN_AND]              = {NULL,                 and_,    PREC_AND      },
    [TOKEN_CLASS]            = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_ELSE]             = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_FALSE]            = {literal,              NULL,    PREC_NONE     },
    [TOKEN_FOR]              = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_FUN]              = {anonymousFunction,    NULL,    PREC_NONE     },
    [TOKEN_IF]               = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_NIL]              = {literal,              NULL,    PREC_NONE     },
    [TOKEN_OR]               = {NULL,                 or_,     PREC_OR       },
    [TOKEN_PRINT]            = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_RETURN]           = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_SUPER]            = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_THIS]             = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_TRUE]             = {literal,              NULL,    PREC_NONE     },
    [TOKEN_VAR]              = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_WHILE]            = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_SWITCH]           = {switchExpression,     NULL,    PREC_NONE     },
    [TOKEN_ERROR]            = {NULL,                 NULL,    PREC_NONE     },
    [TOKEN_EOF]              = {NULL,                 NULL,    PREC_NONE     },
}; 

static void parsePrecedence(const Precedence precedence)
{
    advance();
    const ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL)
    {
        error("Expect expression.");
        return;
    }

    const bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence)
    {
        advance();
        const ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL))
    {
        error("Invalid assignment target.");
    }
}

static ParseRule *getRule(const TokenType type)
{
    return &rules[type];
}

static void turneryExpression() 
{
    const int offset = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    expression();
    const int endJump = emitJump(OP_JUMP);

    consume(TOKEN_COLON, "Expect ':' after expression.");

    patchJump(offset);
    emitByte(OP_POP);
    expression();

    patchJump(endJump);
}

static void expression()
{
    parsePrecedence(PREC_ASSIGNMENT);

    if(match(TOKEN_QUESTIONMARK))
    {
        turneryExpression();
    }
}

static void block()
{
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
    {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expected '}' after block.");
}

static void function(const FunctionType type, ObjString *name)
{
    Compiler compiler;
    initCompiler(&compiler, type, name);
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN))
    {
        do
        {
            current->function->arity++;
            if (current->function->arity > 255)
            {
                errorAtCurrent("Can't have more then 255 parameters.");
            }

            const int constant = parseVariable("Expect parameter name.", false);
            defineVariable(constant, parser.previous.line);
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    const ObjFunction *function = endCompiler();
    emitClosure(function);

    for (int i = 0; i < function->upvalueCount; ++i)
    {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static void method() {
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    const Token *name = &parser.previous;
    int constant = makeIdentifier(name);
    emitIndex(OP_METHOD, constant, name->line);

    FunctionType type = TYPE_FUNCTION;
    ObjString *nameObj = copyString(name->start, name->length);
    function(type, nameObj);
}

static void classDeclaration() {
    const int index = parseVariable("Expect class name.", true);

    const Token* name = &parser.previous;
    const int nameConst = makeIdentifier(name);

    makeInitialized();

    emitBytes(OP_CLASS, nameConst);
    defineVariable(index, name->line);

    namedVariable(*name, false);
    consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        method();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
    emitByte(OP_POP);
}

static void funDeclaration() {
    const int index = parseVariable("Expect function name.", true);
    const Token *name = &parser.previous;
    makeInitialized();
    ObjString *nameObj = copyString(name->start, name->length);
    function(TYPE_FUNCTION, nameObj);
    defineVariable(index, name->line);
}

static void varDeclaration() {
    const int index = parseVariable("Expect variable name.", false);
    const int line = parser.previous.line;

    if (match(TOKEN_EQUAL))
    {
        expression();
    }
    else
    {
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expected ';' after variable declaration.");
    defineVariable(index, line);
}

static void constDeclaration() {
    const int index = parseVariable("Expect variable name.", true);
    const int line = parser.previous.line;

    if (match(TOKEN_EQUAL))
    {
        expression();
    }
    else
    {
        errorAtCurrent("Expected initializer for constant variable.");
        return;
    }

    consume(TOKEN_SEMICOLON, "Expected ';' after variable declaration.");
    defineVariable(index, line);
}

static void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expected ';' after expression.");
    emitByte(OP_POP);
}

static void forStatement() {
    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    int loopVar = -1;
    Token loopVarName;

    if (match(TOKEN_SEMICOLON))
    {
        // No Initializer.
    }
    else if (match(TOKEN_VAR))
    {
        loopVarName = parser.current;
        varDeclaration();
        loopVar = current->localCount - 1;
    }
    else
    {
        expressionStatement();
    }

    ControlFlowContext *ctx =  enterControlFlow(FLOW_LOOP);

    int exitJump = -1;
    if (!match(TOKEN_SEMICOLON))
    {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
    }

    if (!match(TOKEN_RIGHT_PAREN))
    {
        const int bodyJump = emitJump(OP_JUMP);
        const int incrementStart = currentChunk()->count;
        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(OP_LOOP, ctx->innermostLoopStart);
        ctx->innermostLoopStart = incrementStart;
        patchJump(bodyJump);
    }

    int innerVar = -1;
    if (loopVar != -1)
    {
        beginScope();
        emitIndex(OP_GET_LOCAL, loopVar, loopVarName.line);
        addLocal(loopVarName, false);
        makeInitialized();
        innerVar = current->localCount - 1;
    }

    statement();

    if (loopVar != -1)
    {
        emitIndex(OP_GET_LOCAL, innerVar, loopVarName.line);
        emitIndex(OP_SET_LOCAL, innerVar, loopVarName.line);
        emitByte(OP_POP);
        endScope();
    }

    emitLoop(OP_LOOP, ctx->innermostLoopStart);

    if (exitJump != -1)
    {
        patchJump(exitJump);
        emitByte(OP_POP);
    }

    exitControlFlow();
    endScope();
}

static void printStatement()
{
    expression();
    consume(TOKEN_SEMICOLON, "Expected ';' after value.");
    emitByte(OP_PRINT);
}

static void ifStatement()
{
    ControlFlowContext *ctx = enterControlFlow(FLOW_IF);

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect '(' after condition.");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();
    const int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    while (match(TOKEN_ELSE))
    {
        if (match(TOKEN_IF))
        {
            consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
            expression();
            consume(TOKEN_RIGHT_PAREN, "Expect '(' after condition.");

            thenJump = emitJump(OP_JUMP_IF_FALSE);
            emitByte(OP_POP);
            statement();

            const int offset = emitJump(OP_JUMP);
            JumpPatch *patch = malloc(sizeof(JumpPatch));
            patch->jumpOffset = offset;
            patch->next = ctx->breakPatchHead;
            ctx->breakPatchHead = patch;

            patchJump(thenJump);
            emitByte(OP_POP);
        }
        else
        {
            statement();
            break;
        }
    }

    patchJump(elseJump);
    exitControlFlow();
}

static void whileStatement()
{
    enterControlFlow(FLOW_LOOP);

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    const int loopExit = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();
    emitLoop(OP_LOOP, currentControlFlow()->innermostLoopStart);

    patchJump(loopExit);
    emitByte(OP_POP);

    exitControlFlow();
}

static void doWhileStatement()
{
    const int offset = emitJump(OP_JUMP);
    enterControlFlow(FLOW_LOOP);

    emitByte(OP_POP);
    patchJump(offset);

    statement();

    consume(TOKEN_WHILE, "Expect 'while' after loop body.");
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    consume(TOKEN_SEMICOLON, "Expected ';' after 'do while' loop.");

    emitLoop(OP_LOOP_IF_FALSE, currentControlFlow()->innermostLoopStart);
    emitByte(OP_POP);
}

static void repeatStatement()
{
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'repeat'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    enterControlFlow(FLOW_LOOP);

    emitIndex(OP_DEC_LOCAL, current->localCount, parser.previous.line);
    emitByte(1);
    emitByte(OP_CONSTANT_0);
    emitBytes(OP_LESS, OP_NOT);
    const int loopExit = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    statement();
    emitLoop(OP_LOOP, currentControlFlow()->innermostLoopStart);

    patchJump(loopExit);
    emitByte(OP_POP);

    exitControlFlow();
}

static void switchStatement()
{
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'switch'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    consume(TOKEN_LEFT_BRACE, "Expect '{' for switch body.");

    beginScope();
    enterControlFlow(FLOW_SWITCH);

    int caseExit = -1;
    while (match(TOKEN_CASE))
    {
        if (caseExit != -1)
        {
            patchJump(caseExit);
            emitByte(OP_POP);
        }

        expression();
        consume(TOKEN_COLON, "Expect ':' after condition.");

        caseExit = emitJump(OP_JUMP_IF_NOT_EQUAL);
        emitByte(OP_POP);

        while (!check(TOKEN_CASE) && !check(TOKEN_DEFAULT) && !check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
        {
            statement();
        }
    }

    if (match(TOKEN_EOF))
    {
        error("Expect closing '}' after switch body.");
        return;
    }

    if (caseExit != -1)
    {
        patchJump(caseExit);
        emitByte(OP_POP);
    }

    if (match(TOKEN_DEFAULT))
    {
        consume(TOKEN_COLON, "Expect ':' after 'default'.");
        while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
        {
            statement();
        }
        if (match(TOKEN_EOF))
        {
            error("Expect closing '}' after switch body.");
            return;
        }
    }

    consume(TOKEN_RIGHT_BRACE, "Expect closing '}' after switch body.");
    exitControlFlow();
    emitByte(OP_POP);
    endScope();
}

static ControlFlowContext *searchControlFlow(const FlowKind type)
{
    for (int i = current->controlFlowTop; i >= 0; i--)
    {
        if (current->controlFlowStack[i].kind == type)
        {
            return &current->controlFlowStack[i];
        }
    }
    return NULL;
}

static void continueStatement()
{
    const ControlFlowContext *ctx = searchControlFlow(FLOW_LOOP);
    if (ctx == NULL)
    {
        error("Can't use 'continue' outside of a loop.");
        return;
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");

    emitPopTo(ctx->innermostScopeDepth);
    emitLoop(OP_LOOP, ctx->innermostLoopStart);
}

static void breakStatement()
{
    ControlFlowContext *ctx = searchControlFlow(FLOW_LOOP);
    if (ctx == NULL)
    {
        ctx = searchControlFlow(FLOW_SWITCH);
        if (ctx == NULL)
        {
            error("Can't use 'break' outside of a loop or switch.");
            return;
        }
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after 'break'.");

    emitPopTo(ctx->innermostScopeDepth);

    const int offset = emitJump(OP_JUMP);
    JumpPatch *patch = malloc(sizeof(JumpPatch));
    patch->jumpOffset = offset;
    patch->next = ctx->breakPatchHead;
    ctx->breakPatchHead = patch;
}

static void returnStatement()
{
    if (current->type == TYPE_SCRIPT)
    {
        error("Can't return from top-level code.");
    }

    if (match(TOKEN_SEMICOLON))
    {
        emitReturn();
    }
    else
    {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(OP_RETURN);
    }
}

static void synchronize()
{
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF)
    {
        if (parser.previous.type == TOKEN_SEMICOLON)
            return;
        switch (parser.current.type)
        {
        case TOKEN_CLASS:
        case TOKEN_FUN:
        case TOKEN_VAR:
        case TOKEN_FOR:
        case TOKEN_IF:
        case TOKEN_WHILE:
        case TOKEN_PRINT:
        case TOKEN_RETURN:
            return;
        default:
            break;
        }

        advance();
    }
}

static void statement()
{
    if (match(TOKEN_PRINT))
    {
        printStatement();
    }
    else if (match(TOKEN_FOR))
    {
        forStatement();
    }
    else if (match(TOKEN_IF))
    {
        ifStatement();
    }
    else if (match(TOKEN_WHILE))
    {
        whileStatement();
    }
    else if (match(TOKEN_DO))
    {
        doWhileStatement();
    }
    else if (match(TOKEN_REPEAT))
    {
        repeatStatement();
    }
    else if (match(TOKEN_SWITCH))
    {
        switchStatement();
    }
    else if (match(TOKEN_CONTINUE))
    {
        continueStatement();
    }
    else if (match(TOKEN_BREAK))
    {
        breakStatement();
    }
    else if (match(TOKEN_RETURN))
    {
        returnStatement();
    }
    else if (match(TOKEN_LEFT_BRACE))
    {
        beginScope();
        block();
        endScope();
    }
    else
    {
        expressionStatement();
    }
}

static void declaration()
{
    if(match(TOKEN_CLASS)) {
        classDeclaration();
    }
    else if (match(TOKEN_FUN)) {
        funDeclaration();
    }
    else if (match(TOKEN_VAR)) {
        varDeclaration();
    }
    else if (match(TOKEN_CONST)) {
        constDeclaration();
    }
    else {
        statement();
    }

    if (parser.panicMode)
        synchronize();
}

ObjFunction *compile(const char *source)
{
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT, NULL);

    parser.hadError = false;
    parser.panicMode = false;

    advance();

    while (!match(TOKEN_EOF))
    {
        declaration();
    }

    ObjFunction *function = endCompiler();
    return parser.hadError ? NULL : function;
}

void markCompilerRoots() {
    Compiler* compiler = current;
    while (compiler != NULL) {
        markObject((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}
