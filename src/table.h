#ifndef _HOBBYL_TABLE_H
#define _HOBBYL_TABLE_H

#include "common.h"
#include "value.h"
#include "object.h"

void hl_initTable(struct hl_Table* table);
void hl_freeTable(struct hl_State* H, struct hl_Table* table);
bool hl_tableSet(
    struct hl_State* H, struct hl_Table* table, struct hl_String* key, hl_Value value);
bool hl_tableGet(
    struct hl_Table* table, struct hl_String* key, hl_Value* outValue);
bool hl_tableDelete(struct hl_Table* table, struct hl_String* key);
struct hl_String* hl_tableFindString(
    struct hl_Table* table, const char* chars, s32 length, u32 hash);
void hl_tableRemoveUnmarked(struct hl_Table* table);
void hl_copyTable(struct hl_State* H, struct hl_Table* dest, struct hl_Table* src);
void hl_markTable(struct hl_State* H, struct hl_Table* table);

#endif // _HOBBYL_TABLE_H
