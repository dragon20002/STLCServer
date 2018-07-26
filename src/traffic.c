#include "server.h"
#include "traffic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void green_light(TrafficLight* tls) {
    tls->leds[EAST] = 1;
    tls->leds[WEST] = 0;
    tls->leds[SOUTH] = 0;
    tls->leds[NORTH] = 0;
}

void orange_light(TrafficLight* tls) {
    tls->leds[EAST] = 0;
    tls->leds[WEST] = 0;
    tls->leds[SOUTH] = 1;
    tls->leds[NORTH] = 0;
}

void red_light(TrafficLight* tls) {
    tls->leds[EAST] = 0;
    tls->leds[WEST] = 0;
    tls->leds[SOUTH] = 0;
    tls->leds[NORTH] = 1;
}

void green_left_light(TrafficLight* tls) {
    tls->leds[EAST] = 0;
    tls->leds[WEST] = 1;
    tls->leds[SOUTH] = 0;
    tls->leds[NORTH] = 0;
}
