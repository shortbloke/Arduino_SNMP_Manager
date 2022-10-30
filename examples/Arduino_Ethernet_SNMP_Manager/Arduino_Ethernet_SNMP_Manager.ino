#include <Arduino_SNMP_Manager.h>

#include <Ethernet.h>
#include <EthernetUdp.h>

//************************************
//* Ardunio Boards                   *
//************************************
// This sketch and associated libraries, require quite a bit of memory. Thus are more suited to the Mega/Mega 2560.
// It may be possible to create a stripped down version of the libraries with just the capabilities required
// in order to work with boards with less program and dynamic memory.

//************************************
//* Ethernet Info                    *
//************************************
byte mac[] = {0xA8, 0x61, 0x0A, 0xAE, 0x64, 0x29}; //Ethernet shield's MAC

//************************************

//************************************
//* SNMP Device Info                 *
//************************************
IPAddress router(192, 168, 200, 1);
const char *community = "public";
const int snmpVersion = 1; // SNMP Version 1 = 0, SNMP Version 2 = 1
// OIDs
const char *oidIfSpeedGauge = ".1.3.6.1.2.1.10.94.1.1.4.1.2.4"; // Gauge ADSL Down Sync Speed (interface 4)
// const char *oidIfSpeedGauge = ".1.3.6.1.2.1.2.2.1.5.4";         // Gauge Regular ethernet interface ifSpeed.4
const char *oidInOctetsCount32 = ".1.3.6.1.2.1.2.2.1.10.4"; // Counter32 ifInOctets.4
const char *oidUptime = ".1.3.6.1.2.1.1.3.0";               // TimeTicks uptime (hundredths of seconds)
//************************************

//************************************
//* Settings                         *
//************************************
int pollInterval = 10000; // delay in milliseconds
//************************************

//************************************
//* Initialise                       *
//************************************
// Variables
long unsigned int ifSpeedResponse = 0;
long unsigned int inOctetsResponse = 0;
unsigned int uptime = 0;
unsigned int lastUptime = 0;
unsigned long pollStart = 0;
unsigned long intervalBetweenPolls = 0;
float bandwidthInUtilPct = 0;
long unsigned int lastInOctets = 0;
// SNMP Objects
EthernetUDP udp;                                       // UDP object used to send and receive packets
SNMPManager snmp = SNMPManager(community);             // Starts an SMMPManager to listen to replies to get-requests
SNMPGet snmpRequest = SNMPGet(community, snmpVersion); // Starts an SMMPGet instance to send requests
// Blank callback pointer for each OID
ValueCallback *callbackIfSpeed;
ValueCallback *callbackInOctets;
ValueCallback *callbackUptime;
//************************************

//************************************
//* Function declarations            *
//************************************
void getSNMP();
void doSNMPCalculations();
void printVariableValues();
//************************************

void setup()
{
  Serial.begin(38400);
  Ethernet.begin(mac);
  while (!Serial);
  Serial.print("Establishing network connection... ");
  
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
  } 
  else
  {
    Serial.print("IP address: ");
    Serial.println(Ethernet.localIP());
  }

  snmp.setUDP(&udp); // give snmp a pointer to the UDP object
  snmp.begin();      // start the SNMP Manager

  // Get callbacks from creating a handler for each of the OID
  callbackIfSpeed = snmp.addGaugeHandler(router, oidIfSpeedGauge, &ifSpeedResponse);
  callbackInOctets= snmp.addCounter32Handler(router, oidInOctetsCount32, &inOctetsResponse);
  callbackUptime = snmp.addTimestampHandler(router, oidUptime, &uptime);
}

void loop()
{
  // put your main code here, to run repeatedly:
  snmp.loop();
  intervalBetweenPolls = millis() - pollStart;
  if (intervalBetweenPolls >= pollInterval)
  {
    pollStart += pollInterval; // this prevents drift in the delays
    getSNMP();
    doSNMPCalculations(); // Do something with the data collected
    printVariableValues(); // Print the values to the serial console
  }
}

void doSNMPCalculations()
{

  if (uptime == lastUptime)
  {
    Serial.println("Data not updated between polls");
    return;
  }
  else if (uptime < lastUptime)
  { // Check if device has rebooted which will reset counters
    Serial.println("Uptime < lastUptime. Device restarted?");
  }
  else
  {
    if (inOctetsResponse > 0 && ifSpeedResponse > 0 && lastInOctets > 0)
    {
      if (inOctetsResponse > lastInOctets)
      {
        bandwidthInUtilPct = ((float)((inOctetsResponse - lastInOctets) * 8) / (float)(ifSpeedResponse * ((uptime - lastUptime) / 100)) * 100);
      }
      else if (lastInOctets > inOctetsResponse)
      {
        Serial.println("inOctets Counter wrapped");
        bandwidthInUtilPct = (((float)((4294967295 - lastInOctets) + inOctetsResponse) * 8) / (float)(ifSpeedResponse * ((uptime - lastUptime) / 100)) * 100);
      }
    }
  }
  // Update last samples
  lastUptime = uptime;
  lastInOctets = inOctetsResponse;
}

void getSNMP()
{
  // Build a SNMP get-request add each OID to the request
  snmpRequest.addOIDPointer(callbackIfSpeed);
  snmpRequest.addOIDPointer(callbackInOctets);
  snmpRequest.addOIDPointer(callbackUptime);

  snmpRequest.setIP(Ethernet.localIP()); //IP of the Arduino
  snmpRequest.setUDP(&udp);
  snmpRequest.setRequestID(rand() % 5555);
  snmpRequest.sendTo(router);
  snmpRequest.clearOIDList();
}

void printVariableValues()
{
    Serial.print(F("Bandwidth In Utilisation %:"));
    Serial.println(bandwidthInUtilPct);
    Serial.print(F("ifSpeedResponse: "));
    Serial.println(ifSpeedResponse);
    Serial.print(F("inOctetsResponse:"));
    Serial.println(inOctetsResponse);
    Serial.print(F("Uptime: "));
    Serial.println(uptime);
    Serial.println(F("----------------------"));
}