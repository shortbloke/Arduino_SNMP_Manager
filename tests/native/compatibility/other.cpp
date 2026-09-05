#include <Arduino_SNMP_Manager.h>
int compatibilityValue() {
    IntegerType value(128);
    unsigned char buffer[4];
    if (value.serialise(buffer, sizeof(buffer)) != 4) return -1;
    return buffer[3];
}
