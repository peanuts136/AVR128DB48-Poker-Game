/*
 * SPI.c
 *
 * Created: 11/19/2025 3:12:55 PM
 */ 

#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include "SPI.h"

#define LOAD_LOW()   (PORTA.OUTCLR = PIN7_bm) // toggle MAX7219 Load
#define LOAD_HIGH()  (PORTA.OUTSET = PIN7_bm)


static const uint8_t font[10][8] = {
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}, // 0
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // 1
    {0x3C,0x66,0x06,0x1C,0x30,0x60,0x7E,0x00}, // 2
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, // 3
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00}, // 4
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, // 5
    {0x3C,0x60,0x7C,0x66,0x66,0x66,0x3C,0x00}, // 6
    {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}, // 7
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, // 8
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00}  // 9
};


// transmit one byte on SPI0 and wait for completion 
static void SPI_Send(uint8_t d){
    SPI0.DATA = d;
    while(!(SPI0.INTFLAGS & SPI_IF_bm));
}


// send register write to all MAX7219 modules in one CS window
static void sendAll(uint8_t reg, const uint8_t data[NUM_MODULES]){
    LOAD_LOW();
    for(uint8_t i=0;i<NUM_MODULES;i++){
        SPI_Send(reg); // reg is target register number ( 1 - 8 for row, 0x09 decode mode, 0x0A birghtness, 0x0F display)
        SPI_Send(data[i]); 
    }
    LOAD_HIGH(); // latch all at once 
}

void SPI_Init(void){
    PORTA.DIRSET = PIN4_bm | PIN6_bm | PIN7_bm;
    PORTA.OUTSET = PIN7_bm;

    SPI0.CTRLA = SPI_ENABLE_bm | SPI_MASTER_bm | SPI_PRESC_DIV16_gc;
    SPI0.CTRLB = SPI_MODE_0_gc;
    _delay_ms(50);

    uint8_t same[NUM_MODULES];
    for(uint8_t i=0;i<NUM_MODULES;i++) same[i]=0x00;
    sendAll(0x0F,same); // Display test off
    for(uint8_t i=0;i<NUM_MODULES;i++) same[i]=0x01;
    sendAll(0x0C,same); // Normal operation
    for(uint8_t i=0;i<NUM_MODULES;i++) same[i]=0x07;
    sendAll(0x0B,same); // Scan limit 8
    for(uint8_t i=0;i<NUM_MODULES;i++) same[i]=0x08;
    sendAll(0x0A,same); // Medium brightness
    for(uint8_t i=0;i<NUM_MODULES;i++) same[i]=0x00;
    sendAll(0x09,same); // No-decode
    Matrix_Clear();
}


// clear all modules by writing 0 to rows 1-8
void Matrix_Clear(void){
    uint8_t zeros[NUM_MODULES]={0};
    for(uint8_t r=1;r<=8;r++) sendAll(r,zeros);
}


// displays value 0 - 9999 number across modules 
void Matrix_DisplayNumber(uint16_t value)
{
    static uint16_t last_value = 65535; // impossible initial

    // deadband: only redraw if change >= 2
    if(value == last_value) return;
    if(value > last_value && value - last_value < 2) return;
    if(value < last_value && last_value - value < 2) return;
    last_value = value;


    if(value == 0){ Matrix_Clear(); return; } // clears display if 0
    if(value > 9999) value = 9999; // 4 digits 

	// extract digits least significant first 
    uint8_t digits[NUM_MODULES] = {0}; 
    for(uint8_t i=0;i<NUM_MODULES;i++){
        digits[i] = value % 10;
        value /= 10;
    }

    // Draw each row
    for(uint8_t row=0; row<8; row++){
        uint8_t rowdata[NUM_MODULES];
        for(uint8_t d=0; d<NUM_MODULES; d++)
            rowdata[d] = font[digits[NUM_MODULES-1-d]][row];
        sendAll(row+1,rowdata);
    }

    _delay_ms(150);  // visible update speed
}



