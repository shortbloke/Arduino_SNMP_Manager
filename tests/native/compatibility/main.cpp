#include <Arduino_SNMP_Manager.h>
#include <SNMPTable.h>
int compatibilityValue();
int main()
{
    SNMPManager manager;
    SNMPGet request("public", SNMPVersion::Version2c);
    manager.setUDP(nullptr);
    UDP udp;
    SNMPClient client(udp);
    SNMPDevice device(client, "192.168.1.10", "public");
    SNMPRead<SystemUptime> uptime(device);
    SNMPInterfaceRead<2> interfaces(device);
    SNMPSet<1> write(device);
    if (!client.begin().ok() || !uptime.start().ok())
        return 1;
    client.loop(0);
    uptime.cancel();
    if (!interfaces.start().ok())
        return 1;
    interfaces.cancel();
    if (!write.addValue(".1.3.6.1.2.1.1.7.0", SNMPValue::integer32(0)).ok())
        return 1;
    return manager.begin() || compatibilityValue() != 128;
}
