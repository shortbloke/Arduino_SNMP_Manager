#include <Arduino_SNMP_Manager.h>
int compatibilityValue();
int main()
{
    SNMPManager manager;
    SNMPGet request("public", SNMPVersion::Version2c);
    manager.setUDP(nullptr);
    return manager.begin() || compatibilityValue() != 128;
}
