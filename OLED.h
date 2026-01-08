/*
 * OLED.h
 *
 * Created: 11/19/2025 3:12:55 PM
 * Author : ziyan mei
 */ 

#ifndef OLED_H
#define OLED_H

#include <stdint.h>

#define OLED_WIDTH   128
#define OLED_HEIGHT   64
#define OLED_PAGES   (OLED_HEIGHT/8)
#define OLED_ADDR    0x3C   // 7-bit I2C address

typedef enum { BUS_TWI0 = 0, BUS_TWI1 = 1 } OLED_Bus;

void OLED_init(OLED_Bus bus);
void OLED_clear(OLED_Bus bus);
void OLED_update(OLED_Bus bus);
void OLED_setCursor(uint8_t col, uint8_t row);
void OLED_print(const char *str);
void OLED_ShowPlayer(OLED_Bus bus, uint8_t player,
const char *card1, const char *card2,
uint16_t balance);

#endif

