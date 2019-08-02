//===--------------------------------------------------------------------------------------------===
// repl.c - REPL interface utilities
// This source is part of TermUtils
//
// Created on 2019-07-22 by Amy Parent <amy@amyparent.com>
// Copyright (c) 2019 Amy Parent
// Licensed under the MIT License
// =^•.•^=
//===--------------------------------------------------------------------------------------------===
#include <term/repl.h>
#include <term/hexes.h>
#include "string.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>


#define CTL(c)      ((c) & 037)
#define IS_CTL(c)   ((c) && (c) < ' ')
#define DE_CTL(c)   ((c) + '@')

typedef enum REPLAction {
    REPL_SUBMIT,
    REPL_DONE,
    REPL_CLEAR,
    REPL_DONOTHING,
} REPLAction;

typedef enum {
    LINE_DONE,
    LINE_RETURN,
    LINE_REFRESH,
    LINE_CLEAR,
    LINE_WAIT
} LineStatus;

typedef REPLAction (*REPLCallback)(int);

typedef struct {
    int key;
    REPLCallback function;
} REPLCommand;

struct {
    int historyIndex;
    TermREPL* history;
    const char* prompt;
    int cursor;
    String buffer;
} Line;

void termREPLInit(TermREPL* repl) {
    assert(repl && "cannot initialise a null REPL");
    repl->historyCount = 0;
    for(int i = 0; i < TERM_MAX_HISTORY; ++i) {
        repl->history[i] = NULL;
    }

}

void termREPLDeinit(TermREPL* repl) {
    for(char** str = repl->history; str != repl->history + TERM_MAX_HISTORY; ++str) {
        if(!*str) break;
        free(*str);
        *str = NULL;
    }
}

static char* strip(char* data) {
    char* end = data;
    while(*end) end += 1;
    if(*end != '\n' && *end != ' ' && *end != '\0') return data;

    for(; end > data; --end) {
        if(end[-1] != '\n' && end[-1] != ' ') break;
    }
    *end = '\0';
    return data;
}

// MARK: - Printing functions

void showPrompt() {
    printf("%s> ", Line.prompt);
}

void replPut(char c) {
    putchar(c);
}

void replPuts(const char* str) {
    while(*str)
        replPut(*(str++));
}

int replShow(int c) {
    if(IS_CTL(c)) {
        termColorFG(stdout, kTermBlack);
        replPut('^');
        replPut(DE_CTL(c));
        termColorFG(stdout, kTermDefault);
        return 2;
    }
    replPut(c & 0x7f);
    return 1;
}

int replShows(const char* str) {
    int total = 0;
    while(*str)
        total += replShow(*str++);
    return total;
}

// MARK: - Terminal Handling

void startREPL(const char* prompt, TermREPL* history) {
    hexesStartRawMode();
    Line.cursor = 0;
    Line.historyIndex = -1;
    Line.history = history;
    Line.prompt = prompt;
    stringInit(&Line.buffer);
}

void stopREPL() {
    hexesStopRawMode();
    stringDeinit(&Line.buffer);
}

static REPLAction replLeft(int key) {
    if(!Line.cursor) return REPL_DONOTHING;
    if(IS_CTL(Line.buffer.data[Line.cursor-1])) {
        replPut('\b');
    }
    replPut('\b');
    Line.cursor -= 1;
    return REPL_DONOTHING;
}

static REPLAction replRight(int key) {
    if(Line.cursor >= Line.buffer.count) return REPL_DONOTHING;
    replShow(Line.buffer.data[Line.cursor]);
    Line.cursor += 1;
    return REPL_DONOTHING;
}

// MARK: - Insertion and deletion

static void lineCap() {
    int move = replShows(&Line.buffer.data[Line.cursor]);
    replPuts("\e[0K");
    while(move--) replPut('\b');
}

static void replace(const char* str) {
    stringSet(&Line.buffer, str);
    Line.cursor = Line.buffer.count;
    replPut('\r');
    replPuts("\r\e[2K");
    showPrompt();
    replShows(Line.buffer.data);
}

static REPLAction replBackspace(int key) {
    if(!Line.cursor) return REPL_DONOTHING;
    replLeft(0);
    stringErase(&Line.buffer, Line.cursor, 1);
    lineCap();
    return REPL_DONOTHING;
}

static REPLAction replDelete(int key) {
    if(Line.cursor >= Line.buffer.count) return REPL_DONOTHING;
    stringErase(&Line.buffer, Line.cursor, 1);
    lineCap();
    return REPL_DONOTHING;
}

static REPLAction replFlush(int key) {
    replPuts("\r\e[2K");
    return REPL_CLEAR;
}

static REPLAction replCancel(int key) {
    replShow(key);
    replPuts("\n\r");
    return REPL_CLEAR;
}

static REPLAction replReturn(int key) {
    replPuts("\n\r");
    stringAppend(&Line.buffer, '\n');
    return REPL_SUBMIT;
}

static REPLAction replEnd(int key) {
    replShow(key);
    replPuts("\n\r");
    return REPL_DONE;
}

static REPLAction replHistoryPrev(int key) {
    if(Line.historyIndex >= Line.history->historyCount - 1) return REPL_DONOTHING;

    Line.historyIndex += 1;
    replace(Line.history->history[Line.historyIndex]);
    return REPL_DONOTHING;
}

static REPLAction replHistoryNext(int key) {
    if(Line.historyIndex <= 0) {
        Line.historyIndex = -1;
        return replFlush(key);
    }

    Line.historyIndex -= 1;
    replace(Line.history->history[Line.historyIndex]);
    return REPL_DONOTHING;
}

void termREPLRecord(TermREPL* repl, const char* entry) {
    int length = strlen(entry);
    memmove(repl->history+1, repl->history, (TERM_MAX_HISTORY-1) * sizeof(char*));
    repl->history[0] = malloc((1 + length) * sizeof(char));
    memcpy(repl->history[0], entry, length);
    repl->history[0][length] = '\0';
    strip(repl->history[0]);

    repl->historyCount += 1;
    if(repl->historyCount > TERM_MAX_HISTORY) repl->historyCount = TERM_MAX_HISTORY;
}

static const REPLCommand dispatch[] = {
    {CTL('d'),          replEnd},
    {CTL('l'),          replFlush},
    {CTL('c'),          replCancel},
    {CTL('h'),          replBackspace},
    {CTL('m'),          replReturn},

    {KEY_BACKSPACE,     replBackspace},
    {KEY_DELETE,        replDelete},

    {CTL('b'),          replLeft},
    {KEY_ARROW_LEFT,    replLeft},
    {CTL('f'),          replRight},
    {KEY_ARROW_RIGHT,   replRight},

    {CTL('p'),          replHistoryPrev},
    {KEY_ARROW_UP,      replHistoryPrev},

    {CTL('n'),          replHistoryNext},
    {KEY_ARROW_DOWN,    replHistoryNext},
};

static REPLAction defaultCMD(int key) {
    stringInsert(&Line.buffer, Line.cursor, key & 0x00ff);
    replShow(key);
    Line.cursor += 1;
    if(Line.cursor == Line.buffer.count) return REPL_DONOTHING;
    int move = replShows(&Line.buffer.data[Line.cursor]);
    while(move--) replPut('\b');
    return REPL_DONOTHING;
}

const REPLCallback findCommand(int c) {
    static const int count = sizeof(dispatch) / sizeof(dispatch[0]);
    for(int i = 0; i < count; ++i) {
        if(dispatch[i].key == c) return dispatch[i].function;
    }
    return defaultCMD;
}

char* termREPL(TermREPL* repl, const char* prompt) {
    startREPL(prompt, repl);
    showPrompt();

    char* result = NULL;
    for(;;) {
        int key = hexesGetKeyRaw();
        REPLAction action = findCommand(key)(key);

        switch(action) {
        case REPL_SUBMIT:
            result = stringTake(&Line.buffer);
            goto done;

        case REPL_DONE:
            result = NULL;
            goto done;

        case REPL_CLEAR:
            Line.cursor = 0;
            Line.buffer.count = 0;
            if(Line.buffer.capacity) Line.buffer.data[0] = '\0';
            showPrompt();
            break;

        case REPL_DONOTHING:
            break;
        }
    }
done:
    stopREPL();
    fflush(stdout);
    return result;
    // stopREPL();
}
