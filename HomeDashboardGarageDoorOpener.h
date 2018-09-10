/* GarageDoor-Opener.h */ 
#ifndef GARAGEDOOR_OPENER_H
#define GARAGEDOOR_OPENER_H

#define SERIAL_BAUD 115200

// Pin layout

#define OPEN_SWITCH_PIN     D1  // input button to open the door
#define CLOSE_SWITCH_PIN    D2  // input button to close the door

// D3 is reserved during programming
#define LIGHT_RELAY_PIN     D4  // The pin that turns the light on / off
#define OPEN_MOTOR_PIN      D5  // The pin that activates the open door motor
#define CLOSE_MOTOR_PIN     D6  // The pin that activates the close door motor 
#define PHOTOCELL_INPUT_PIN D7  // Stops closing 
#define NETWORK_STATUS_PIN  D8

#define PRESSED             LOW     // LOW if you use built in pull up resistor
#define UNPRESSED           HIGH    // HIGH if you use built in pull up resistor
#define INPUT_PINMODE       INPUT_PULLUP    // built in pull up resistor


#define NETWORK_STATUS_NOT_CONNECTED        0
#define NETWORK_STATUS_ONLY_WIFI            256
#define NETWORK_STATUS_CONNECTING_TO_MQTT   512
#define NETWORK_STATUS_CONNECTED            1023

#define MQTT_TOPIC_REGISTRATION    "/homedashboard/device/registration"

#define GARAGE_DOOR_MQTT_CLIENT_ID "garage-door"

#include <Arduino.h>

typedef enum {
    GD_OPEN,
    GD_CLOSED,
    GD_OPENING,
    GD_CLOSING,
    GD_PARTIALLY_OPENED,
    GD_UNKNOWN
} GarageDoorState;

#endif
