#include "scanner.h"
#include <string.h>

#include "common.h"

typedef struct {
    const char *start;
    const char *current;
    int line;
    int braceCount;
} Scanner;

Scanner scanner;

void initScanner(const char *source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

static Token makeToken(const TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int) (scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

static Token errorToken(const char *message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int) strlen(message);
    token.line = scanner.line;
    return token;
}

static bool isAtEnd() {
    return *scanner.current == '\0';
}

static char advance() {
    scanner.current++;
    return scanner.current[-1];
}

static bool match(const char c) {
    if (isAtEnd())
        return false;
    if (*scanner.current != c)
        return false;
    scanner.current++;
    return true;
}

static char peek() {
    return *scanner.current;
}

static char peekNext() {
    if (isAtEnd())
        return '\0';
    return scanner.current[1];
}

static bool isDigit(const char c) {
    return c >= '0' && c <= '9';
}

static bool isAlpha(const char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static void skipWhitespace() {
    for (;;) {
        const char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                scanner.line++;
                advance();
                break;
            case '/': {
                const char next = peekNext();
                if (next == '/') {
                    while (peek() != '\n' && !isAtEnd()) advance();
                    break;
                }
                if (next == '*') {
                    while (peek() != '*' && peekNext() != '/' && !isAtEnd()) advance();
                    break;
                }
                return;
            }
            default:
                return;
        }
    }
}

static TokenType checkKeyword(const int start, const int length, const char *rest, const TokenType type) {
    if (scanner.current - scanner.start == start + length && memcmp(scanner.start + start, rest, length) == 0) {
        return type;
    }

    return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
    switch (scanner.start[0]) {
        case 'a':
            return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'b':
            return checkKeyword(1, 4, "reak", TOKEN_BREAK);
        case 'c':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a':
                        return checkKeyword(2, 2, "se", TOKEN_CASE);
                    case 'l':
                        return checkKeyword(2, 3, "ass", TOKEN_CLASS);
                    case 'o': {
                        if (scanner.start[2] == 'n') {
                            switch (scanner.start[3]) {
                                case 's':
                                    return checkKeyword(4, 1, "t", TOKEN_CONST);
                                case 't':
                                    return checkKeyword(4, 4, "inue", TOKEN_CONTINUE);
                            }
                        }
                    }
                }
            }
            break;
        case 'd':
            if (scanner.current - scanner.start > 1) {
                if (scanner.start[1] == 'o') {
                    return TOKEN_DO;
                }
                return checkKeyword(1, 6, "efault", TOKEN_DEFAULT);
            }
        case 'e':
            return checkKeyword(1, 3, "lse", TOKEN_ELSE);
        case 'i':
            return checkKeyword(1, 1, "f", TOKEN_IF);
        case 'n':
            return checkKeyword(1, 2, "il", TOKEN_NIL);
        case 'o':
            return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'p':
            return checkKeyword(1, 4, "rint", TOKEN_PRINT);
        case 'r':
            if (scanner.current - scanner.start > 1) {
                if (scanner.start[1] == 'e') {
                    switch (scanner.start[2]) {
                        case 't':
                            return checkKeyword(3, 3, "urn", TOKEN_RETURN);
                        case 'p':
                            return checkKeyword(3, 3, "eat", TOKEN_REPEAT);
                    }
                }
            }
        case 's':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 't':
                        return checkKeyword(2, 4, "atic", TOKEN_STATIC);
                    case 'u':
                        return checkKeyword(2, 3, "per", TOKEN_SUPER);
                    case 'w':
                        return checkKeyword(2, 4, "itch", TOKEN_SWITCH);
                }
            }
        case 'v':
            return checkKeyword(1, 2, "ar", TOKEN_VAR);
        case 'w':
            return checkKeyword(1, 4, "hile", TOKEN_WHILE);
        case 'f':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a':
                        return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o':
                        return checkKeyword(2, 1, "r", TOKEN_FOR);
                    case 'u':
                        return checkKeyword(2, 1, "n", TOKEN_FUN);
                }
            }
            break;
        case 't':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'h':
                        return checkKeyword(2, 2, "is", TOKEN_THIS);
                    case 'r':
                        return checkKeyword(2, 2, "ue", TOKEN_TRUE);
                }
            }
            break;
    }

    return TOKEN_IDENTIFIER;
}

static Token string() {
    while (peek() != '"' && peek() != '$' && !isAtEnd()) {
        if (peek() == '\n')
            scanner.line++;
        advance();
    }

    if (isAtEnd())
        return errorToken("Unterminated string.");

    if (peek() == '$') {
        advance();
        if (peek() == '{') {
            scanner.braceCount++;
            const Token token = makeToken(TOKEN_INTERPOLATION);
            advance();
            return token;
        }

        return errorToken("Expected { after $ in string interpolation.");
    }

    advance();
    return makeToken(TOKEN_STRING);
}

static Token number() {
    while (isDigit(peek()))
        advance();

    if (peek() == '.' && isDigit(peekNext())) {
        advance();
        while (isDigit(peek()))
            advance();
    }

    return makeToken(TOKEN_NUMBER);
}

static Token identifier() {
    while (isAlpha(peek()) || isDigit(peek()))
        advance();
    return makeToken(identifierType());
}

Token scanToken() {
    skipWhitespace();
    scanner.start = scanner.current;

    if (isAtEnd())
        return makeToken(TOKEN_EOF);

    const char c = advance();
    if (isAlpha(c))
        return identifier();
    if (isDigit(c))
        return number();

    switch (c) {
        case '(':
            return makeToken(TOKEN_LEFT_PAREN);
        case ')':
            return makeToken(TOKEN_RIGHT_PAREN);
        case '{':
            return makeToken(TOKEN_LEFT_BRACE);
        case '}':
            if (scanner.braceCount > 0) {
                scanner.braceCount--;
                return string();
            }
            return makeToken(TOKEN_RIGHT_BRACE);
        case ';':
            return makeToken(TOKEN_SEMICOLON);
        case ',':
            return makeToken(TOKEN_COMMA);
        case '.':
            return makeToken(TOKEN_DOT);
        case ':':
            return makeToken(TOKEN_COLON);
        case '-': {
            if (match('-')) {
                return makeToken(TOKEN_MINUS_MINUS);
            }
            if (match('=')) {
                return makeToken(TOKEN_MINUS_EQUAL);
            }
            return makeToken(TOKEN_MINUS);
        }
        case '+': {
            if (match('+')) {
                return makeToken(TOKEN_PLUS_PLUS);
            }
            if (match('=')) {
                return makeToken(TOKEN_PLUS_EQUAL);
            }
            return makeToken(TOKEN_PLUS);
        }
        case '/':
            return makeToken(TOKEN_SLASH);
        case '%':
            return makeToken(TOKEN_PERCENT);
        case '*':
            return makeToken(TOKEN_STAR);
        case '!':
            return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '|':
            return makeToken(TOKEN_VERTICAL_BAR);
        case '&':
            return makeToken(TOKEN_AND);
        case '^':
            return makeToken(TOKEN_CARET);
        case '?':
            return makeToken(TOKEN_QUESTIONMARK);
        case '=':
            return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '>':
            if (match('>')) {
                return makeToken(TOKEN_GREATER_GREATER);
            }
            return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '<':
            if (match('<')) {
                return makeToken(TOKEN_LESS_LESS);
            }
            return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '"':
            return string();
        default:
            break;
    }

    return errorToken("Unexpected character.");
}
