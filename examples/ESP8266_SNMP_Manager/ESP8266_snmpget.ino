#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Arduino_SNMP.h>

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
char *oidIfSpeedGuage = ".1.3.6.1.2.1.2.2.1.5.4";     // Guage ifSpeed.4
char *oidInOctetsCount32 = ".1.3.6.1.2.1.2.2.1.10.4"; //Counter32 ifInOctets.4
char *oidServiceCountInt = ".1.3.6.1.2.1.1.7.0";      // Integer sysServices
char *oidSysNameInt = ".1.3.6.1.2.1.1.5.0";           // OctetString SysName
char *oid64Counter = ".1.3.6.1.2.1.31.1.1.1.6.4";     //Counter64 64-bit ifInOctets.4
char *oidUptime = ".1.3.6.1.2.1.1.3.0";               //TimeTicks
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
int timeLast = 0;
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
  snmp.addStringHandler(oidSysNameInt, &sysNameResponse);
  snmp.addCounter64Handler(oid64Counter, &hcCounter);
  snmp.addTimestampHandler(oidUptime, &uptime);

  // Create the call back ID's for each OID
  callbackIfSpeed = snmp.findCallback(oidIfSpeedGuage);
  callbackInOctets = snmp.findCallback(oidInOctetsCount32);
  callbackServices = snmp.findCallback(oidServiceCountInt);
  callbackSysName = snmp.findCallback(oidSysNameInt);
  callback64Counter = snmp.findCallback(oid64Counter);
  callbackUptime = snmp.findCallback(oidUptime);

  timeLast = millis();
}

void loop()
{
  // put your main code here, to run repeatedly:
  snmp.loop();
  getSNMP();
}

void getSNMP()
{
  // Check to see if it is time to send an SNMP request.
  if ((timeLast + pollInterval) <= millis())
  {

    //build a SNMP get-request
    snmpRequest.addOIDPointer(callbackIfSpeed);
    snmpRequest.setIP(WiFi.localIP()); //IP of the arduino
    snmpRequest.setUDP(&udp);
    snmpRequest.setRequestID(rand() % 5555);
    snmpRequest.sendTo(router);
    snmpRequest.clearOIDList();

    snmpRequest.addOIDPointer(callbackInOctets);
    snmpRequest.setIP(WiFi.localIP()); //IP of the arduino
    snmpRequest.setUDP(&udp);
    snmpRequest.setRequestID(rand() % 5555);
    snmpRequest.sendTo(router);
    snmpRequest.clearOIDList();

    snmpRequest.addOIDPointer(callbackServices);
    snmpRequest.setIP(WiFi.localIP()); //IP of the arduino
    snmpRequest.setUDP(&udp);
    snmpRequest.setRequestID(rand() % 5555);
    snmpRequest.sendTo(router);
    snmpRequest.clearOIDList();

    snmpRequest.addOIDPointer(callbackSysName);
    snmpRequest.setIP(WiFi.localIP()); //IP of the arduino
    snmpRequest.setUDP(&udp);
    snmpRequest.setRequestID(rand() % 5555);
    snmpRequest.sendTo(router);
    snmpRequest.clearOIDList();

    snmpRequest.addOIDPointer(callback64Counter);
    snmpRequest.setIP(WiFi.localIP()); //IP of the arduino
    snmpRequest.setUDP(&udp);
    snmpRequest.setRequestID(rand() % 5555);
    snmpRequest.sendTo(router);
    snmpRequest.clearOIDList();

    snmpRequest.addOIDPointer(callbackUptime);
    snmpRequest.setIP(WiFi.localIP()); //IP of the arduino
    snmpRequest.setUDP(&udp);
    snmpRequest.setRequestID(rand() % 5555);
    snmpRequest.sendTo(router);
    snmpRequest.clearOIDList();

    //see the results of the get-request in the serial monitor
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

    timeLast = millis();
  }
}