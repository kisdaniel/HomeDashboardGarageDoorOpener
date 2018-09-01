# HomeDashboard Garage Door Opener

Home Dashboard Garage Door Opener Arduino project for NodeMCU (ESP8266)

This program connect to an MQTT server and receives commands from mqtt and publishes status of the garage door to mqtt.

# Configuration

## Wifi, mqtt

Wifi and mqtt server is configured in an embedded captive portal, find a HomeDashboardConfigAP access point and connect to the network and use "password" for password.

You can setup wifi and mqtt server host and port.

If you want to reconfigure your nodemcu you have to follow the following instcructions:
- press reset button (open and close button must not be pressed)
- press open and close button within 2 sec after reset
- find HomeDashboardConfigAP access point and connect to it, wifi password is "password" 
- type http://192.168.4.1 to your browser

## Default Pin layout

Pin layout definied in HomeDashboardGarageDoorOpener.h 

* D3 - not used: reserved during programming 
* D4 - light relay control
* D5 - activates open relay 
* D6 - activates close relay
* D7 - push button: starts opening the door
* D8 - push button: starts closing the door

https://www.homedashboard.hu/
