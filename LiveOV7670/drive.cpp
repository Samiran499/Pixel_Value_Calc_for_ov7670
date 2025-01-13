#include "drive.h"

void declarePins() {
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT);
    pinMode(IN4, OUTPUT);
}

void drive(uint16_t& column_intensities){
    uint16_t max = 0;

    for(int i = 0; i < 5; i++) {
        max = (max < column_intensities[i])? column_intensities[i] : max;
    }

    if(max = 0) { // If the brightest pixel is on the leftmost portion
        digitalWrite(IN1, HIGH);
        digitalWrite(IN2, LOW);
        digitalWrite(IN3, LOW);
        digitalWrite(IN4, HIGH);
        delay(500);
    }
    if(max = 1) { // If the brightest pixel is on the second leftmost portion
        digitalWrite(IN1, HIGH);
        digitalWrite(IN2, LOW);
        digitalWrite(IN3, LOW);
        digitalWrite(IN4, HIGH);
        delay(300);
    }
    if(max = 2) { // If the brightest pixel is on the centre
        digitalWrite(IN1, HIGH);
        digitalWrite(IN2, LOW);
        digitalWrite(IN3, HIGH);
        digitalWrite(IN4, LOW);
        delay(500);
    }
    if(max = 3) { // If the brightest pixel is on the second rightmost portion
        digitalWrite(IN1, LOW);
        digitalWrite(IN2, HIGH);
        digitalWrite(IN3, HIGH);
        digitalWrite(IN4, LOW);
        delay(300);
    }
    if(max = 4) { // If the brightest pixel is on the rightmost portion
        digitalWrite(IN1, LOW);
        digitalWrite(IN2, HIGH);
        digitalWrite(IN3, HIGH);
        digitalWrite(IN4, LOW);
        delay(500);
    }
}