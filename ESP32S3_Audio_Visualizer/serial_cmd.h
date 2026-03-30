#pragma once

/*******************************************************************************
 * Serial Command Handler — JSON commands over USB CDC Serial
 * 
 * The ESP32-S3 USB CDC serial is used for bidirectional communication with
 * a standalone HTML settings page (opened locally in browser) via Web Serial API.
 * 
 * Protocol:
 *   PC → ESP32:  JSON line, e.g. {"cmd":"set","brightness":128}
 *   ESP32 → PC:  JSON line response, e.g. {"ok":true,"brightness":128}
 *   ESP32 → PC:  periodic status push {"status":true,"fps":10.2,...}
 ******************************************************************************/

void serial_cmd_init();
void serial_cmd_poll();   // call from Core 0 task, reads & processes serial input
