/* GarageDoor-Opener.h */ 
#ifndef GARAGEDOOR_OPENER_H
#define GARAGEDOOR_OPENER_H

#define SERIAL_BAUD 115200

// Pin layout

#define PHOTOCELL_INPUT_PIN D2  // Stops closing 
// D3 is reserved during programming
#define LIGHT_RELAY_PIN     D4  // The pin that turns the light on / off
#define OPEN_MOTOR_PIN      D5  // The pin that activates the open door motor
#define CLOSE_MOTOR_PIN     D6  // The pin that activates the close door motor 
#define OPEN_SWITCH_PIN     D7
#define CLOSE_SWITCH_PIN    D8

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
