#ifndef VarBinds_h
#define VarBinds_h

#include "BER.h"

typedef struct VarBindStruct
{
    /**
     * @brief Destroy a binding wrapper without deleting its borrowed OID or value.
     */
    ~VarBindStruct() {
        // oid and value are borrowed from the response BER tree, which owns them.
    };
    OIDType *oid = 0;
    ASN_TYPE type;
    BER_CONTAINER *value = 0;
} VarBind;

typedef struct VarBindListStruct
{
    /**
     * @brief Delete owned wrappers and successor nodes, but not borrowed response-tree values.
     */
    ~VarBindListStruct();
    struct VarBindStruct *value = 0;
    struct VarBindListStruct *next = 0;
} VarBindList;

#endif