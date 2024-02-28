# Door Control System

This is a system to control the electric strike of your front door using ESPHome and Home Assistant.
It consists of two devices, one using an ESP8266 and another using an Arduino pro mini. These two devices communicate over UART. When the correct ocde is entered on the Arduino, it sends an unlock message on the ESP to trigger the relay and open the door.

Read some extra info on my [website](https://hackermagnet.com/door-control-system-with-esphome-and-home-assistant/)

![door unlocker esphome home assistant](https://github.com/jon-daemon/Door-Control-System/assets/206048/f815cf2f-4d65-42b1-8036-f59ea4d267dd)

## Schematic

## ESP8266 device

This board should not be exposed outside of the door you want to control. This is the device that powers the relay to control the door.

### Connections for the ESP

It gets power from a 5V power supply. A mobile charger could be used as well.
I have added a reed switch to know when the door is open and shut off the relay.
There is another relay that can be used to turn on a lamp manually or using automations (e.g. triggering when the door is open and it's dark outside).
As you can see in the schematic I have added two modules to detect AC voltage. One of them is connected in parallel with the doorbell push button and it can detect presses in case you are using an AC doorbell.
The other AC module is connected in parallel with the electric strike and can identify when it get energized no matter how (using the relay or the push button inside the house).
The red module in the schematic is a logic level converter for the Tx/Rx signals between the ESP and the Arduino.

### ESPHome configuration notes

You need to include the `uart_read_line_sensor.h` to read and send data on the Arduino using UART.
Change the "Op3nD00r" in the configuration with the message you want to receive from the Arduino and unlock the door.

### Home Assistant configuration

You need to create two helpers in Home Assistant to store the state of two entities and retrieve its latest state after a restart.
- `input_number.door_tries_left` stores the tries left after a  wrong keycode attempt.
- `input_number.door_unlock_type` which stores a number indicating how the door was opened the last time. You could use that for statistics
  1: Door opened with a key
  2: Door opened with the push button
  3: Door opened with a keycode or RFID card
  4: Door opened using Home Assistant service

## Arduino device

The second device is Arduino based and is mounted next to your door to unlock it using a code or an RFID card.

Download the enclosure .stl files from here.

### Connections

There is a 4x4 keypad and a buzzer.
The 16x2 LCD is connected using I^2.
The RC522 needs a 3.3V regulator if you power the Arduino from 5V.

### Firmware notes

- You could change some of the settings of the Arduino device using the keypad. The options start with * and end with #. You should read the comments in the Arduino code to understand more about the settings you can change.
- In the Arduino code change the PIN, PUK and “Op3nD00r” with the secret message you want to be used between the two devices. Use the same message in the esphome configuration.
- The AC detection module used for the doorbel is designed for 220V. I replaced the big resistor with 2.2k to use it with the 12V AC signal from the doorbell.

### Manual of Arduino device

| **Command**    | replace **x** | **function**                       |
| -------------- | ------------- | ---------------------------------- |
| \*A**x**#      | 1 or 0        | Light ON \| OFF                    |
| \*B1A**x**#    | 1 - 9         | PIN tries allowed                  |
| \*B2A**x**#    | 1 or 0        | Wait door response from ESP        |
| \*B3A**x**#    | 1 or 0        | Show codes on screen               |
| \*A90**x**#    | 1 or 0        | Buzzer ON / OFF                    |
| \*C10**xxxx**# | 1000 - 9999   | Wait door response in ms           |
| \*C20**xxxx**# | 1000 - 9999   | Text on screen duration in ms      |
| \*C30**xxxx**# | 1000 - 9999   | Keypad active duration in ms       |
| \*C40**xxxx**# | 1000 - 9999   | LCD idle duration in ms            |
| \*DD0**x**#    | 1 or 0        | EEPROM delete / read               |
| \*DBA**x**#    | 1 - 5         | Select RFID card to save or delete |
| \*DCD**x**#    | 1 or 0        | RFID unlocking ON / OFF            |
