#include "table.h"

#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"

#define TABLE_MAX_LOAD 0.75

void hl_initTable(struct hl_Table* table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

void hl_freeTable(struct hl_Table* table) {
  hl_FREE_ARRAY(struct hl_Entry, table->entries, table->capacity);
  hl_initTable(table);
}

static struct hl_Entry* findEntry(
    struct hl_Entry* entries, s32 capacity, struct hl_String* key) {
  u32 index = key->hash & (capacity - 1);
  struct hl_Entry* tombstone = NULL;

  while (true) {
    struct hl_Entry* entry = &entries[index];

    if (entry->key == NULL) {
      if (hl_IS_NIL(entry->value)) {
        return tombstone != NULL ? tombstone : entry;
      } else {
        if (tombstone == NULL) {
          tombstone = entry;
        }
      }
    } else if (entry->key == key) {
      return entry;
    }

    index = (index + 1) & (capacity - 1);
  }
}

static void adjustCapacity(struct hl_Table* table, s32 capacity) {
  struct hl_Entry* entries = hl_ALLOCATE(struct hl_Entry, capacity);
  for (s32 i = 0; i < capacity; i++) {
    entries[i].key = NULL;
    entries[i].value = hl_NEW_NIL;
  }

  table->count = 0;
  for (s32 i = 0; i < table->capacity; i++) {
    struct hl_Entry* entry = &table->entries[i];
    if (entry->key == NULL) {
      continue;
    }

    struct hl_Entry* dst = findEntry(entries, capacity, entry->key);
    dst->key = entry->key;
    dst->value = entry->value;
    table->count++;
  }

  hl_FREE_ARRAY(struct hl_Entry, table->entries, table->capacity);

  table->entries = entries;
  table->capacity = capacity;
}

bool hl_tableSet(
    struct hl_Table* table, struct hl_String* key, hl_Value value) {
  if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
    s32 capacity = hl_GROW_CAPACITY(table->capacity);
    adjustCapacity(table, capacity);
  }

  struct hl_Entry* entry = findEntry(table->entries, table->capacity, key);
  bool isNew = entry->key == NULL;
  if (isNew && hl_IS_NIL(entry->value)) {
    table->count++;
  }

  entry->key = key;
  entry->value = value;
  return isNew;
}

bool hl_tableGet(
    struct hl_Table* table, struct hl_String* key, hl_Value* outValue) {
  if (table->count == 0) {
    return false;
  }

  struct hl_Entry* entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) {
    return false;
  }

  *outValue = entry->value;
  return true;
}

bool hl_tableDelete(struct hl_Table* table, struct hl_String* key) {
  if (table->count == 0) {
    return false;
  }

  struct hl_Entry* entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) {
    return false;
  }

  entry->key = NULL;
  entry->value = hl_NEW_BOOL(true);
  return true;
}

struct hl_String* hl_tableFindString(
    struct hl_Table* table, const char* chars, s32 length, u32 hash) {
  if (table->count == 0) {
    return NULL;
  }

  u32 index = hash & (table->capacity - 1);
  while (true) {
    struct hl_Entry* entry = &table->entries[index];
    if (entry->key == NULL) {
      if (hl_IS_NIL(entry->value)) {
        return NULL;
      }
    } else if (entry->key->length == length
        && entry->key->hash == hash
        && memcmp(entry->key->chars, chars, length) == 0) {
      return entry->key;
    }

    index = (index + 1) & (table->capacity - 1);
  }
}

void hl_tableRemoveUnmarked(struct hl_Table* table) {
  for (s32 i = 0; i < table->capacity; i++) {
    struct hl_Entry* entry = &table->entries[i];
    if (entry->key != NULL && !entry->key->obj.isMarked) {
      hl_tableDelete(table, entry->key);
    }
  }
}

void hl_markTable(struct hl_Table* table) {
  for (s32 i = 0; i < table->capacity; i++) {
    struct hl_Entry* entry = &table->entries[i];
    hl_markObject((struct hl_Obj*)entry->key);
    hl_markValue(entry->value);
  }
}

void hl_copyTable(struct hl_Table* dest, struct hl_Table* src) {
  for (s32 i = 0; i < src->capacity; i++) {
    struct hl_Entry* entry = &src->entries[i];
    if (entry->key != NULL) {
      hl_tableSet(dest, entry->key, entry->value);
    }
  }
}
