#pragma once
#include <Arduino.h>

// Initialize the provisioning web server (call during provisioning state)
void provisioningServerInit();

// Handle incoming HTTP requests (call in main loop during provisioning)
void provisioningServerUpdate();

// Stop the provisioning server
void provisioningServerStop();

// Check if server is running
bool provisioningServerIsRunning();
