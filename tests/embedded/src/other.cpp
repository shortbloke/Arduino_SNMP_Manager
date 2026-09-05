#include <Arduino.h>
#include <Arduino_SNMP_Manager.h>

bool checkSecondTranslationUnit()
{
    Counter64 value(UINT64_MAX);
    unsigned char buffer[11];
    return value.serialise(buffer, sizeof(buffer)) == 11 &&
           value.fromBuffer(buffer, sizeof(buffer));
}
