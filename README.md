# raspberrypi-industrial-gateway
Source code used for Avnet Raspberry Pi Industrial Gateway

# changes from initial release.
Updates to CAN mcp251x driver fixing a problem after everyother reset.
Updates to the Attiny drivers, attiny_mfd, attiny_btn, attiny_led, attiny_wdt. Full support for watchdog and 3 RST button states.

Programmers reference now on all three repositories.  Also, REST API reference is on all three github sites. Programmers reference is SmartEdge-iiot-gateway_programmers_reference.pdf.  The REST API is Smartedge-iiot-gateway-rest-api.pdf


Easily keep up to date if you clone the initial repositorys, then periodically you can run this command on the repository "git diff >update_patch.txt". Then copy update_patch.txt to the gateway and run "git appliy update_patch.txt , be sure you have internet and constant power.

If you want to start with a fresh image then element14 will have the new image smartedge-iiot-gateway-v11-<date of release>.  Please do above step so you are upto date.



