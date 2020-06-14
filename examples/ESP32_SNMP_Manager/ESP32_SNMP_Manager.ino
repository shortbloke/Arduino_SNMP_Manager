#include <WiFi.h>
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
char *oidIfSpeedGuage = ".1.3.6.1.2.1.10.94.1.1.4.1.2.4"; // Guage ADSL Down Sync Speed (interface 4)
// char *oidIfSpeedGuage = ".1.3.6.1.2.1.2.2.1.5.4";         // Guage Regular ethernet interface ifSpeed.4
char *oidInOctetsCount32 = ".1.3.6.1.2.1.2.2.1.10.4"; // Counter32 ifInOctets.4
char *oidServiceCountInt = ".1.3.6.1.2.1.1.7.0";      // Integer sysServices
char *oidSysName = ".1.3.6.1.2.1.1.5.0";              // OctetString SysName
char *oid64Counter = ".1.3.6.1.2.1.31.1.1.1.6.4";     // Counter64 64-bit ifInOctets.4
char *oidUptime = ".1.3.6.1.2.1.1.3.0";               // TimeTicks uptime (hundredths of seconds)
//************************************

//************************************
//* Settings                         *
//************************************
int pollInterval = 10000; // delay in milliseconds
char string[50];          // Maximum length of SNMP get response for String values
//************************************

//************************************
//* Initialise                       *
//************************************
// Variables
unsigned int ifSpeedResponse = 0;
unsigned int inOctetsResponse = 0;
int servicesResponse = 0;
char *sysNameResponse = string;
long long unsigned int hcCounter = 0;
int uptime = 0;
int lastUptime = 0;

unsigned long pollStart = 0;
unsigned long intervalBetweenPolls = 0;
float bandwidthInUtilPct = 0;
unsigned int lastInOctets = 0;
// SNMP Objects
WiFiUDP udp;                                           // UDP object used to send and recieve packets
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
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("my IP address: ");
  Serial.println(WiFi.localIP());

  snmp.setUDP(&udp); // give snmp a pointer to the UDP object
  snmp.begin();      // start the SNMP Manager

  // Create a handler for each of the OID
  snmp.addGuageHandler(oidIfSpeedGuage, &ifSpeedResponse);
  snmp.addCounter32Handler(oidInOctetsCount32, &inOctetsResponse);
  snmp.addIntegerHandler(oidServiceCountInt, &servicesResponse);
  snmp.addStringHandler(oidSysName, &sysNameResponse);
  snmp.addCounter64Handler(oid64Counter, &hcCounter);
  snmp.addTimestampHandler(oidUptime, &uptime);

  // Create the call back ID's for each OID
  callbackIfSpeed = snmp.findCallback(oidIfSpeedGuage);
  callbackInOctets = snmp.findCallback(oidInOctetsCount32);
  callbackServices = snmp.findCallback(oidServiceCountInt);
  callbackSysName = snmp.findCallback(oidSysName);
  callback64Counter = snmp.findCallback(oid64Counter);
  callbackUptime = snmp.findCallback(oidUptime);
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
    // Print out the values collected and calculated
    Serial.print("Bandwidth In Utilisation %: ");
    Serial.println(bandwidthInUtilPct);
    Serial.print("ifSpeedResponse: ");
    Serial.println(ifSpeedResponse);
    Serial.print("inOctetsResponse: ");
    Serial.println(inOctetsResponse);
    Serial.print("servicesResponse: ");
    Serial.println(servicesResponse);
    Serial.print("sysNameResponse: ");
    Serial.println(sysNameResponse);
    Serial.print("Uptime: ");
    Serial.println(uptime);
    Serial.print("HCCounter: ");
    // Print can't handle 64bit (long long unsigned int)
    char buffer[50];
    sprintf(buffer, "%llu", hcCounter);
    Serial.println(buffer);
    Serial.println("----------------------");
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
  // Only enable if you have an 64 bit counter to query.
  // FIXME: Currently crashes if 64bit counter not found
  //  snmpRequest.addOIDPointer(callback64Counter);
  snmpRequest.addOIDPointer(callbackUptime);

  snmpRequest.setIP(WiFi.localIP()); //IP of the arduino
  snmpRequest.setUDP(&udp);
  snmpRequest.setRequestID(rand() % 5555);
  snmpRequest.sendTo(router);
  snmpRequest.clearOIDList();
}
