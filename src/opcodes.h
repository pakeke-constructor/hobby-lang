#ifndef _HOBBYL_OPCODES
#define _HOBBYL_OPCODES

enum Bytecode {
  BC_CONSTANT,
  BC_NIL,
  BC_TRUE,
  BC_FALSE,
  BC_POP,
  BC_ARRAY,
  BC_GET_SUBSCRIPT,
  BC_SET_SUBSCRIPT,
  BC_DEFINE_GLOBAL,
  BC_GET_GLOBAL,
  BC_SET_GLOBAL,
  BC_GET_UPVALUE,
  BC_SET_UPVALUE,
  BC_GET_LOCAL,
  BC_SET_LOCAL,
  BC_INIT_PROPERTY,
  BC_GET_STATIC,
  BC_PUSH_PROPERTY,
  BC_GET_PROPERTY,
  BC_SET_PROPERTY,
  BC_DESTRUCT_ARRAY,
  BC_EQUAL,
  BC_NOT_EQUAL,
  BC_GREATER,
  BC_GREATER_EQUAL,
  BC_LESSER,
  BC_LESSER_EQUAL,
  BC_CONCAT,
  BC_ADD,
  BC_SUBTRACT,
  BC_MULTIPLY,
  BC_DIVIDE,
  BC_MODULO,
  BC_POW,
  BC_NEGATE,
  BC_NOT,
  BC_JUMP,
  BC_JUMP_IF_FALSE,
  BC_INEQUALITY_JUMP,
  BC_LOOP,
  BC_CALL,
  BC_INSTANCE,
  BC_CLOSURE,
  BC_CLOSE_UPVALUE,
  BC_RETURN,
  BC_ENUM,
  BC_ENUM_VALUE,
  BC_STRUCT,
  BC_STRUCT_FIELD,
  BC_METHOD,
  BC_STATIC_METHOD,
  BC_INVOKE,
  BC_BREAK,
};

#endif // _HOBBYL_OPCODES
