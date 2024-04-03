#ifndef _HOBBYL_TABLE_H
#define _HOBBYL_TABLE_H

#include "common.h"
#include "object.h"

void initTable(struct Table* table);
void freeTable(struct State* H, struct Table* table);
bool tableSet(
    struct State* H, struct Table* table, struct String* key, Value value);
bool tableGet(
    struct Table* table, struct String* key, Value* outValue);
bool tableDelete(struct Table* table, struct String* key);
struct String* tableFindString(
    struct Table* table, const char* chars, s32 length, u32 hash);
void tableRemoveUnmarked(struct Table* table);
void copyTable(struct State* H, struct Table* dest, struct Table* src);
void markTable(struct State* H, struct Table* table);

#endif // _HOBBYL_TABLE_H
