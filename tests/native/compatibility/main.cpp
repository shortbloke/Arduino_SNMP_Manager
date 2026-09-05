#include <Arduino_SNMP_Manager.h>
int compatibilityValue();
int main()
{
    SNMPManager manager;
    SNMPGet request("public", 1);
    manager.setUDP(nullptr);
    return manager.begin() || compatibilityValue() != 128;
}
