#if defined(ESP8266)
#include <ESP8266WiFi.h> // ESP8266 Core WiFi Library         
#else
#include <WiFi.h>        // ESP32 Core WiFi Library    
#endif

#include <WiFiUdp.h>
#include <Arduino_SNMP_Manager.h>

//************************************
//* Your WiFi info                   *
//************************************
const char *ssid = "SSID";
const char *password = "PASSWORD";
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
const char *oidServiceCountInt = ".1.3.6.1.2.1.1.7.0";      // Integer sysServices
const char *oidSysName = ".1.3.6.1.2.1.1.5.0";              // OctetString SysName
const char *oid64Counter = ".1.3.6.1.2.1.31.1.1.1.6.4";     // Counter64 64-bit ifInOctets.4
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
unsigned int ifSpeedResponse = 0;
unsigned int inOctetsResponse = 0;
int servicesResponse = 0;
char sysName[50];
char *sysNameResponse = sysName;
long long unsigned int hcCounter = 0;
unsigned int uptime = 0;
unsigned int lastUptime = 0;
unsigned long pollStart = 0;
unsigned long intervalBetweenPolls = 0;
float bandwidthInUtilPct = 0;
unsigned int lastInOctets = 0;
// SNMP Objects
WiFiUDP udp;                                           // UDP object used to send and receive packets
SNMPManager snmp = SNMPManager(community);             // Starts an SMMPManager to listen to replies to get-requests
SNMPGet snmpRequest = SNMPGet(community, snmpVersion); // Starts an SMMPGet instance to send requests
// Blank callback pointer for each OID
ValueCallback *callbackIfSpeed;
ValueCallback *callbackInOctets;
ValueCallback *callbackServices;
ValueCallback *callbackSysName;
ValueCallback *callback64Counter;
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
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.println("");
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to SSID: ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  snmp.setUDP(&udp); // give snmp a pointer to the UDP object
  snmp.begin();      // start the SNMP Manager

  // Get callbacks from creating a handler for each of the OID
  callbackIfSpeed = snmp.addGaugeHandler(router, oidIfSpeedGauge, &ifSpeedResponse);
  callbackInOctets= snmp.addCounter32Handler(router, oidInOctetsCount32, &inOctetsResponse);
  callbackServices = snmp.addIntegerHandler(router, oidServiceCountInt, &servicesResponse);
  callbackSysName = snmp.addStringHandler(router, oidSysName, &sysNameResponse);
  callback64Counter = snmp.addCounter64Handler(router, oid64Counter, &hcCounter);
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
  snmpRequest.addOIDPointer(callbackServices);
  snmpRequest.addOIDPointer(callbackSysName);
  snmpRequest.addOIDPointer(callback64Counter);
  snmpRequest.addOIDPointer(callbackUptime);

  snmpRequest.setIP(WiFi.localIP()); // IP of the listening MCU
  // snmpRequest.setPort(501);  // Default is UDP port 161 for SNMP. But can be overriden if necessary.
  snmpRequest.setUDP(&udp);
  snmpRequest.setRequestID(rand() % 5555);
  snmpRequest.sendTo(router);
  snmpRequest.clearOIDList();
}

void printVariableValues()
{
    Serial.printf("Bandwidth In Utilisation %%: %.1f\n", bandwidthInUtilPct);
    Serial.printf("ifSpeedResponse: %d\n", ifSpeedResponse);
    Serial.printf("inOctetsResponse: %d\n", inOctetsResponse);
    Serial.printf("servicesResponse: %d\n", servicesResponse);
    Serial.printf("sysNameResponse: %s\n", sysNameResponse);
    Serial.printf("Uptime: %d\n", uptime);
    Serial.printf("HCCounter: %llu\n", hcCounter);
    Serial.println("----------------------");
}