#if defined(ESP8266)
#include <ESP8266WiFi.h> // ESP8266 Core WiFi Library
#else
#include <WiFi.h> // ESP32 Core WiFi Library
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
const char *community = "public"; // SNMP Community string
const int snmpVersion = 1;        // Enum, SNMP Version 1 = 0, SNMP Version 2 = 1
// OIDs
const char *oidSysName = ".1.3.6.1.2.1.1.5.0"; // OctetString SysName
const char *oidUptime = ".1.3.6.1.2.1.1.3.0";  // TimeTicks uptime (hundredths of seconds)
//************************************

//************************************
//* Settings                         *
//************************************
int devicePollInterval = 100;    // delay in milliseconds
int lastDeviceWaitPeriod = 5000; // delay in milliseconds
#define LOWEROCTETLIMIT 1        // Set the lowest IP address to 1, .0 typically isn't used and isn't well supported.
#define UPPEROCTETLIMIT 6        // Set the upper limit of the range of IPs to query
//************************************

//************************************
//* Initialise                       *
//************************************
// Structures
struct device
{
  IPAddress address;
  char name[50];
  char *sysName = name; // StringHandler needs pointer to char*
  unsigned int uptime;
}; // Structure for the device records

// Global Variables
struct device deviceRecords[UPPEROCTETLIMIT + 1]; // Array of device records. _1 as we're not using the 0 index in the array.
int lastOctet = LOWEROCTETLIMIT;                  // Initialise last octet to lowest IP
bool allDevicesPolled = false;                    // Flag to to indicate all devices have been sent SNMP requests. Note responses may not yet have arrived.
// Initialise variables used for timer counters to zero.
unsigned long devicePollStart = 0;
unsigned long intervalBetweenDevicePolls = 0;
unsigned long intervalBetweenLastDeviceReadyPolls = 0;
unsigned long deviceReadyStart = 0;

// SNMP Objects
WiFiUDP udp;                                           // UDP object used to send and receive packets
SNMPManager snmp = SNMPManager(community);             // Starts an SNMPManager to listen to replies to get-requests
SNMPGet snmpRequest = SNMPGet(community, snmpVersion); // Starts an SNMPGet instance to send requests
ValueCallback *callbackSysName;                        // Callback pointer for each OID
ValueCallback *callbackUptime;                         // Callback pointer for each OID
//************************************

//************************************
//* Function declarations            *
//************************************
void sendSNMPRequest(IPAddress, struct device *deviceRecord);
int getNextOctet(int current);
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
  Serial.printf("\nConnected to SSID: %s - IP Address: ", ssid);
  Serial.println(WiFi.localIP());

  snmp.setUDP(&udp); // give snmp a pointer to the UDP object
  snmp.begin();      // start the SNMP Manager
}

void loop()
{
  snmp.loop();                                                       // Needs to be called frequently to process incoming SNMP responses.
  intervalBetweenDevicePolls = millis() - devicePollStart;           // Timer for triggering per device Polls
  intervalBetweenLastDeviceReadyPolls = millis() - deviceReadyStart; // Timer to trigger end of polling all devices (allowing some time for final packets to be processed)
  if (allDevicesPolled)
  {
    if (intervalBetweenLastDeviceReadyPolls > lastDeviceWaitPeriod) // Waiting time after all devices polled complete?
    {
      deviceReadyStart += lastDeviceWaitPeriod; // This prevents drift in the delays
      printVariableValues();                    // Print the values to the serial console
      allDevicesPolled = false;                 // Reset the flag
    }
  }
  else
  {
    if (intervalBetweenDevicePolls >= devicePollInterval) // Time to poll the next device?
    {
      devicePollStart += devicePollInterval;                // This prevents drift in the delays
      IPAddress deviceIP(192, 168, 200, lastOctet);         // Set IP address to be queried. Note: This simple example will only work with the last Octet of the address changing as this is used as a simple index for the device records.
      sendSNMPRequest(deviceIP, &deviceRecords[lastOctet]); // Function call to send SNMP requests to specified device. Values will be stored in the deviceRecords array when they are returned (async)
      lastOctet = getNextOctet(lastOctet);                  // Update to the next IP address to be queried
    }
  }
}

void sendSNMPRequest(IPAddress target, struct device *deviceRecord)
{
  Serial.print("sendSNMPRequest - target: ");
  Serial.println(target);
  deviceRecord->address = target;
  // Get callbacks from creating a handler for each of the OID
  callbackSysName = snmp.addStringHandler(target, oidSysName, &deviceRecord->sysName);
  callbackUptime = snmp.addTimestampHandler(target, oidUptime, &deviceRecord->uptime);

  // Build a SNMP get-request add each OID to the request
  snmpRequest.addOIDPointer(callbackSysName);
  snmpRequest.addOIDPointer(callbackUptime);

  snmpRequest.setIP(WiFi.localIP()); // IP of the listening MCU
  snmpRequest.setUDP(&udp);
  snmpRequest.setRequestID(rand() % 5555);
  snmpRequest.sendTo(target);
  snmpRequest.clearOIDList();
}

int getNextOctet(int current)
{
  if (current == UPPEROCTETLIMIT)
  {
    allDevicesPolled = true;
    return LOWEROCTETLIMIT;
  }
  return current + 1;
}

void printVariableValues()
{
  int i;
  for (i = LOWEROCTETLIMIT; i <= UPPEROCTETLIMIT; i++)
  {
    Serial.print("Address: ");
    Serial.print(deviceRecords[i].address);
    Serial.print(" - Name: ");
    Serial.print(deviceRecords[i].name);
    Serial.print(" - Uptime: ");
    Serial.print(deviceRecords[i].uptime);
    Serial.println();
  }
}
