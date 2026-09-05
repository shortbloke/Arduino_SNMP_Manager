#include "VarBinds.h"

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
