#include <FS.h> //this needs to be first, or it all crashes and burns...

// the following override is not working due to this issue: https://github.com/knolleary/pubsubclient/pull/282
// #define MQTT_MAX_PACKET_SIZE    254
// you need to change PubSubClient.h MQTT_MAX_PACKET_SIZE to 254

#include "HomeDashboardGarageDoorOpener.h"

#include <ESP8266WiFi.h> //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h> //https://github.com/bblanchon/ArduinoJson

#include <PubSubClient.h>

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40];
char mqtt_port[6] = "1883";
char mqtt_user[100];
char mqtt_password[100];
char device_name[34] = "GarageDoorOpener";
char open_close_timeout[6] = "45000";

char inTopic[60];
char outTopic[60];

//flag for saving data
bool mqttFailedBefore = false;

DynamicJsonBuffer jsonBuffer(512);

WiFiManager wifiManager;
WiFiClient espClient;
PubSubClient client(espClient);

WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 100);
WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 100);
WiFiManagerParameter custom_device_name("deviceName", "Device name", device_name, 32);
WiFiManagerParameter custom_open_close_timeout("openCloseTimeout", "Opening/closing in millisec", open_close_timeout, 6);

GarageDoorState currentState = GD_UNKNOWN;
GarageDoorState previousState = GD_UNKNOWN;
boolean previousOpenPressed = false;
boolean previousClosePressed = false;

boolean lampUserRequestedState = false;

long lastConnectRetry = 0;
long keeplightOnUntil = 0;

long keeplightOnAfterOpeningInMillisec = 20000;
long keeplightOnAfterClosingInMillisec = 30000;

long openAndClosingTimeout = 15000;

long openState = 0;
long lastStateChange = 0;

long previousChangeInMillis = 0;
long lastStateSent = 0;

bool disableLamp = false;
bool prevObstacleOnPhotocell = false;
bool obstacleOnPhotocell = false;
bool lamp = false;

String getDoorStateAsString(GarageDoorState state)
{
  switch (state)
  {
  case GD_OPEN:
    return String("open");
  case GD_CLOSED:
    return String("closed");
  case GD_OPENING:
    return String("opening");
  case GD_CLOSING:
    return String("closing");
  case GD_PARTIALLY_OPENED:
    return String("partiallyOpened");
  case GD_UNKNOWN:
    return String("unknown");
  }

  Serial.println("Garage Door State Value Unknown!");

  return String("unknown");
}

void turnLight(boolean on)
{
  if (disableLamp)
  {
    lamp = false;
    digitalWrite(LIGHT_RELAY_PIN, HIGH);
  }
  else
  {
    lamp = on;
    digitalWrite(LIGHT_RELAY_PIN, on ? LOW : HIGH);
  }
}

void lightLoop()
{
  if (keeplightOnUntil > 0)
  {
    long now = millis();
    if (now > keeplightOnUntil && !lampUserRequestedState)
    {
      keeplightOnUntil = 0;
      turnLight(false);
    }
  }
}

void open()
{
  if (currentState == GD_UNKNOWN)
  {
    openState = 0;
  }
  lastStateChange = millis();

  if (openState >= openAndClosingTimeout)
  {
    openState = openAndClosingTimeout;
    stop(GD_OPEN);
  }
  else
  {
    if (currentState != GD_OPENING)
    {
      stop(GD_PARTIALLY_OPENED);
      digitalWrite(OPEN_MOTOR_PIN, LOW);
    }
  }

  // digitalWrite(CLOSE_MOTOR_PIN, HIGH);
  keeplightOnUntil = lastStateChange + keeplightOnAfterOpeningInMillisec;
  turnLight(true);

  currentState = GD_OPENING;

  publishState();
}

void close()
{
  if (currentState != GD_CLOSING && currentState != GD_CLOSED)
  {
    if (currentState == GD_UNKNOWN)
    {
      openState = openAndClosingTimeout;
    }
    lastStateChange = millis();
    if (openState <= 0)
    {
      stop(GD_CLOSED);
      openState = 0;
    }
    else
    {
      if (currentState != GD_CLOSING)
      {
        stop(GD_PARTIALLY_OPENED);
        digitalWrite(CLOSE_MOTOR_PIN, LOW);
      }
    }
    keeplightOnUntil = lastStateChange + keeplightOnAfterOpeningInMillisec;
    turnLight(true);

    currentState = GD_CLOSING;
  }
}

void stop(GarageDoorState newState)
{
  if (currentState != newState)
  {
    digitalWrite(OPEN_MOTOR_PIN, HIGH);
    digitalWrite(CLOSE_MOTOR_PIN, HIGH);
    switch (currentState)
    {
    case GD_OPENING:
      openState = openState + lastStateChange - millis();
      keeplightOnUntil = millis() + keeplightOnAfterOpeningInMillisec;
      if (openState > openAndClosingTimeout)
      {
        openState = openAndClosingTimeout;
      }
      break;
    case GD_CLOSING:
      openState = openState - (lastStateChange - millis());
      if (openState < 0)
      {
        openState = 0;
      }
      keeplightOnUntil = millis() + keeplightOnAfterClosingInMillisec;
      break;
    default:
      turnLight(lampUserRequestedState);
      break;
    }
    if (currentState != newState)
    {
      currentState = newState;
      publishState();
    }
  }
}

void calculateOpenStateAndStopIfNecessary()
{
  long now;
  switch (currentState)
  {
  case GD_OPENING:
    now = millis();
    openState = openState + now - lastStateChange;
    lastStateChange = now;
    if (openState > openAndClosingTimeout)
    {
      stop(GD_OPEN);
    }
    else if (now - lastStateSent > 1000)
    {
      publishState();
    }

    break;
  case GD_CLOSING:
    now = millis();
    openState = openState - now + lastStateChange;
    lastStateChange = now;
    if (openState < 0)
    {
      stop(GD_CLOSED);
    }
    else if (now - lastStateSent > 1000)
    {
      publishState();
    }

    break;
  default:
    break;
  }
}

void checkButtonCommands()
{
  bool openPressed = digitalRead(OPEN_SWITCH_PIN) == PRESSED;
  bool closePressed = digitalRead(CLOSE_SWITCH_PIN) == PRESSED;
  bool photocell = digitalRead(PHOTOCELL_INPUT_PIN) == PRESSED;

  //Serial.print(openPressed ? "open pressed,   " : "open unpressed, ");
  //Serial.print(closePressed ? "close pressed,   " : "close unpressed, ");
  //Serial.println(photocell ? "photocell pressed" : "photocell unpressed");
  //delay(500);

  long now = millis();

  obstacleOnPhotocell = photocell;

  if (photocell && currentState == GD_CLOSING)
  {
    stop(GD_PARTIALLY_OPENED);
    closePressed = false;
  }
  else if ((previousOpenPressed != openPressed) || (previousClosePressed != closePressed))
  {
    if (previousChangeInMillis + 500 < now)
    {
      if (openPressed && closePressed)
      {
        Serial.println("open and close pressed");
      }
      else if (openPressed)
      {
        Serial.println("open pressed");
        if (currentState == GD_OPENING)
        {
          stop(GD_PARTIALLY_OPENED);
        }
        else
        {
          open();
        }
      }
      else if (closePressed)
      {
        Serial.println("close pressed");
        if (currentState == GD_CLOSING)
        {
          stop(GD_PARTIALLY_OPENED);
        }
        else
        {
          close();
        }
      }
      previousClosePressed = closePressed;
      previousOpenPressed = openPressed;
      previousChangeInMillis = now;
    }
  }
  else if (obstacleOnPhotocell != prevObstacleOnPhotocell)
  {
    publishState();
  }
}

//callback notifying us of the need to save config
void saveConfigCallback()
{
  Serial.println("saving config");

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(device_name, custom_device_name.getValue());
  strcpy(open_close_timeout, custom_open_close_timeout.getValue());

  JsonObject &json = jsonBuffer.createObject();
  json["mqtt_server"] = mqtt_server;
  json["mqtt_port"] = mqtt_port;
  json["mqtt_user"] = mqtt_user;
  json["mqtt_password"] = mqtt_password;

  json["device_name"] = device_name;
  json["open_close_timeout"] = open_close_timeout;

  openAndClosingTimeout = atoi(open_close_timeout);

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile)
  {
    Serial.println("failed to open config file for writing");
  }
  json.printTo(Serial);
  json.printTo(configFile);
  configFile.close();
  jsonBuffer.clear();
  //end save
}

void loadConfig()
{
  Serial.println("mounting FS...");
  if (SPIFFS.begin())
  {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json"))
    {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile)
      {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        JsonObject &json = jsonBuffer.parseObject(buf.get());

        json.printTo(Serial);

        if (json.success())
        {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(device_name, json["device_name"]);
          strcpy(open_close_timeout, json["open_close_timeout"]);
          openAndClosingTimeout = atoi(open_close_timeout);
          if (openAndClosingTimeout == 0)
          {
            openAndClosingTimeout = 15000;
          }
        }
        else
        {
          Serial.println("failed to load json config");
        }
        configFile.close();
        jsonBuffer.clear();
      }
    }
    else
    {
      Serial.println("No config file present");
    }
  }
  else
  {
    Serial.println("failed to mount FS");
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (strcmp(topic, MQTT_TOPIC_REGISTRATION) == 0)
  {
    if (length == 2 && strncmp((char *)payload, "{}", length) == 0)
    {
      // resends a registration message
      deviceRegistration();
    }
    else
    {
      Serial.println("skip other device registration");
    }
  }
  else
  {
    JsonObject &inputObject = jsonBuffer.parseObject(payload);
    const char *command = inputObject["command"];
    if (strcmp(command, "open") == 0)
    {
      open();
    }
    else if (strcmp(command, "close") == 0)
    {
      close();
    }
    else if (strcmp(command, "lighton") == 0)
    {
      long timeout = atol(inputObject["timeoutInMillis"]);
      if (timeout > 0)
      {
        keeplightOnUntil = timeout + millis();
      }
      else
      {
        keeplightOnUntil = 0;
      }
      turnLight(true);
      publishState();
    }
    else if (strcmp(command, "lightoff") == 0)
    {
      Serial.println("turn light off");
      turnLight(false);
      jsonBuffer.clear();
      Serial.println("publish state");
      publishState();
    }
    else if (strcmp(command, "disableLight") == 0)
    {
      disableLamp = true;
      turnLight(false);
      jsonBuffer.clear();
      publishState();
    }
    else if (strcmp(command, "enableLight") == 0)
    {
      disableLamp = false;
    }
    else if (strcmp(command, "stop") == 0)
    {
      stop(GD_PARTIALLY_OPENED);
    }
    else if (strcmp(command, "heartbeat") == 0)
    {
      jsonBuffer.clear();
      publishState();
    }
    else if (strcmp(command, "settings") == 0)
    {
      keeplightOnAfterOpeningInMillisec = atoi(inputObject["keeplightOnAfterOpening"]);
      keeplightOnAfterClosingInMillisec = atoi(inputObject["keeplightOnAfterClosing"]);
      openAndClosingTimeout = atoi(inputObject["openAndClosingTimeout"]);
      if (openState > openAndClosingTimeout)
      {
        openState = openAndClosingTimeout;
      }
      // TODO: save to eprom
    }
    jsonBuffer.clear();
  }
}

void deviceRegistration()
{
  JsonObject &json = jsonBuffer.createObject();
  json["name"] = device_name;
  json["type"] = "Gate";

  char jsonChar[160];
  json.printTo((char *)jsonChar, json.measureLength() + 1);
  Serial.print("Register device (");
  Serial.print(MQTT_TOPIC_REGISTRATION);
  Serial.print("), result: ");
  Serial.println(client.publish(MQTT_TOPIC_REGISTRATION, jsonChar));
}

void reconnectToMqtt()
{
  Serial.print("trying to connect to MQTT server (");
  Serial.print(device_name);

  client.setCallback(mqttCallback);
  client.setServer(mqtt_server, atoi(mqtt_port));

  analogWrite(NETWORK_STATUS_PIN, NETWORK_STATUS_CONNECTING_TO_MQTT);

  boolean connectResult = false;

  if (strlen(mqtt_user) > 0)
  {
    Serial.print(", user: ");
    Serial.print(mqtt_user);
    connectResult = client.connect(device_name, mqtt_user, mqtt_password);
  }
  else
  {
    connectResult = client.connect(device_name);
  }
  Serial.println(")");

  lastConnectRetry = millis();

  if (connectResult)
  {
    Serial.println("Connected to MQTT");
    mqttFailedBefore = false;

    deviceRegistration();
    client.subscribe(MQTT_TOPIC_REGISTRATION);
    strcpy(inTopic, "/homedashboard/");
    strcat(inTopic, device_name);
    strcpy(outTopic, inTopic);
    strcat(inTopic, "/in");
    strcat(outTopic, "/out");

    client.subscribe(inTopic);
    Serial.print("Subscribe to ");
    Serial.println(inTopic);
    publishState();
    analogWrite(NETWORK_STATUS_PIN, NETWORK_STATUS_CONNECTED);
  }
  else
  {
    analogWrite(NETWORK_STATUS_PIN, NETWORK_STATUS_ONLY_WIFI);
    if (!mqttFailedBefore)
    {
      Serial.print("Failed to connect to MQTT server: ");
      Serial.print(mqtt_server);
      Serial.print(", port: ");
      Serial.println(atoi(mqtt_port));
    }
    mqttFailedBefore = true;
  }
}

void connectToMqttIfNotConnected()
{
  if (!client.connected())
  {
    long now = millis();
    if (lastConnectRetry + 30000 < now)
    {
      reconnectToMqtt();
    }
  }
  else
  {
    client.loop();
  }
}

void publishState()
{
  if (client.connected())
  {
    prevObstacleOnPhotocell = obstacleOnPhotocell;
    JsonObject &json = jsonBuffer.createObject();
    json["name"] = device_name;
    json["state"] = getDoorStateAsString(currentState);
    char position[10];
    itoa(openState, position, 10);
    json["position"] = position;
    json["lightDisabled"] = disableLamp ? true : false;
    json["light"] = lamp ? true : false;
    json["photocell"] = obstacleOnPhotocell ? true : false;

    char jsonChar[200];
    json.printTo((char *)jsonChar, json.measureLength() + 1);
    int result = client.publish(outTopic, jsonChar, json.measureLength());

    // Serial.print("publish state to topic:");
    // Serial.print(outTopic);
    // Serial.print(", length: ");
    // Serial.print(json.measureLength());

    // Serial.print(", result: ");
    // Serial.println(result);

    // Serial.println(jsonChar);
    // Serial.println(openState);

    jsonBuffer.clear();

    lastStateSent = millis();
  }
}

void initPins()
{
  Serial.println("init pins");
  pinMode(OPEN_MOTOR_PIN, OUTPUT);
  pinMode(CLOSE_MOTOR_PIN, OUTPUT);
  pinMode(LIGHT_RELAY_PIN, OUTPUT);
  pinMode(NETWORK_STATUS_PIN, OUTPUT);

  pinMode(PHOTOCELL_INPUT_PIN, INPUT_PINMODE);
  pinMode(OPEN_SWITCH_PIN, INPUT_PINMODE);
  pinMode(CLOSE_SWITCH_PIN, INPUT_PINMODE);

  digitalWrite(OPEN_MOTOR_PIN, HIGH);
  digitalWrite(CLOSE_MOTOR_PIN, HIGH);
  digitalWrite(LIGHT_RELAY_PIN, HIGH);
  analogWrite(NETWORK_STATUS_PIN, NETWORK_STATUS_NOT_CONNECTED);

  Serial.println("waiting 2 sec...");
  delay(2000);
  bool openPressed = digitalRead(OPEN_SWITCH_PIN) == PRESSED;
  bool closePressed = digitalRead(CLOSE_SWITCH_PIN) == PRESSED;
  if (openPressed && closePressed)
  {

    Serial.println("open and close pressed...");
    flashLedIn();
    flashLedOut();

    openPressed = digitalRead(OPEN_SWITCH_PIN) == PRESSED;
    closePressed = digitalRead(CLOSE_SWITCH_PIN) == PRESSED;
    if (openPressed && closePressed)
    {
      flashLedIn();
      flashLedOut();

      Serial.println("reset requested...");
      // SPIFFS.format();
      wifiManager.resetSettings();
    }
  }
  analogWrite(NETWORK_STATUS_PIN, NETWORK_STATUS_NOT_CONNECTED);

  //attachInterrupt(digitalPinToInterrupt(PHOTOCELL_INPUT_PIN), photocellpinChange, CHANGE);
  //attachInterrupt(digitalPinToInterrupt(OPEN_SWITCH_PIN), openSwitch_PIN, CHANGE);
}

void flashLedOut()
{
  for (int i = 1023; i > 0; i--)
  {
    analogWrite(NETWORK_STATUS_PIN, i);
    delay(1);
  }
}
void flashLedIn()
{
  for (int i = 0; i < 1024; i++)
  {
    analogWrite(NETWORK_STATUS_PIN, i);
    delay(1);
  }
}

void setup()
{
  Serial.begin(115200);

  Serial.println(MQTT_MAX_PACKET_SIZE);
  Serial.println("starting...");

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json

  loadConfig();

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_device_name);
  wifiManager.addParameter(&custom_open_close_timeout);

  WiFi.hostname(device_name);
  wifiManager.setConfigPortalTimeout(60 * 3);

  initPins();

  if (!wifiManager.autoConnect("HomeDashboardConfigAP", "password"))
  {
    Serial.println("failed to connect and hit timeout, you should reset to reconfigure");
  }
  else
  {
    Serial.println("connected...");
    Serial.print("local ip: ");
    Serial.println(WiFi.localIP());
  }

  //read updated parameters

  //save the custom parameters to FS
}

void reconnectWiFi()
{
  if (WiFi.reconnect())
  {
    analogWrite(NETWORK_STATUS_PIN, NETWORK_STATUS_ONLY_WIFI);
    Serial.print("successfully reconnected, local ip:");
    Serial.println(WiFi.localIP());
    connectToMqttIfNotConnected();
  }
}

void loop()
{
  if (!WiFi.isConnected() && currentState != GD_OPENING && currentState != GD_CLOSING)
  {
    analogWrite(NETWORK_STATUS_PIN, NETWORK_STATUS_NOT_CONNECTED);
    Serial.println("Wifi connection lost...");
    reconnectWiFi();
  }
  else
  {
    connectToMqttIfNotConnected();
  }
  checkButtonCommands();
  lightLoop();
  calculateOpenStateAndStopIfNecessary();
}