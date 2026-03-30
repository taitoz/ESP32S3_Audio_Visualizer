#pragma once

/*******************************************************************************
 * Web Server — WiFi AP + ESPAsyncWebServer for settings UI
 * 
 * Runs entirely on Core 0 via async handlers. Zero impact on Core 1.
 * User connects to WiFi AP and opens 192.168.4.1 in browser.
 ******************************************************************************/

void web_server_init();   // start WiFi AP + HTTP server
