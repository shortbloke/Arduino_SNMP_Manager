#include "VarBinds.h"

// List nodes own their VarBind wrappers, while each wrapper borrows its OID and
// value from the response tree. Delete wrappers only, and unlink iteratively to
// avoid recursive destruction on small embedded stacks.
VarBindListStruct::~VarBindListStruct()
{
    while (next)
    {
        auto *node = next;
        next = node->next;
        node->next = nullptr;
        delete node;
    }
    delete value;
    value = 0;
}
