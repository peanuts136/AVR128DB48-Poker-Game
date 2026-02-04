/*
 * SPI.h
 * Created: 11/19/2025 3:12:55 PM
 */ 

#ifndef SPI_H_
#define SPI_H_

#include <avr/io.h>
#include <stdint.h>

#define NUM_MODULES 4   // Four 8×8 MAX7219 matrices

void SPI_Init(void);
void Matrix_Clear(void);
void Matrix_DisplayNumber(uint16_t value);

#endif

