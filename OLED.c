/*
 * OLED.c
 *
 * Created: 11/19/2025 3:12:55 PM
 * Author : ziyan mei
 */ 

#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#include <string.h>
#include "OLED.h"


// initialize TWI as 100 kHz master w internal pull ups
static void TWI0_init(void){
	PORTA.PIN2CTRL = PORT_PULLUPEN_bm; // SDA0
	PORTA.PIN3CTRL = PORT_PULLUPEN_bm; // SCL0
	TWI0.MBAUD = (F_CPU/(2*100000UL)) - 5; // standard mode baud
	TWI0.MCTRLA = TWI_ENABLE_bm; // enable master
	TWI0.MSTATUS = TWI_BUSSTATE_IDLE_gc; // force idle
}
static void TWI1_init(void){
	PORTF.PIN2CTRL = PORT_PULLUPEN_bm; // SDA1
	PORTF.PIN3CTRL = PORT_PULLUPEN_bm; // SCL1
	TWI1.MBAUD = (F_CPU/(2*100000UL)) - 5;
	TWI1.MCTRLA = TWI_ENABLE_bm;
	TWI1.MSTATUS = TWI_BUSSTATE_IDLE_gc;
}


// send single SSD1306 cmd on selected bus
static void OLED_write_cmd(OLED_Bus bus, uint8_t cmd){
	TWI_t *twi = (bus==BUS_TWI0)? &TWI0 : &TWI1;
	twi->MADDR = (OLED_ADDR<<1); // start and write to device address
	while(!(twi->MSTATUS & TWI_WIF_bm)); // wait for address ack
	twi->MDATA = 0x00;            // control byte = command
	while(!(twi->MSTATUS & TWI_WIF_bm)); // wait byte ack
	twi->MDATA = cmd;
	while(!(twi->MSTATUS & TWI_WIF_bm));
	twi->MCTRLB = TWI_MCMD_STOP_gc; // stop
}

// framebuffer covering entire 128x64 screen page layout
static uint8_t fb[OLED_WIDTH * OLED_PAGES]; // 128 * 8 = 1024 bytes
static uint8_t cur_col=0, cur_row=0; // 6 x 8 text cursor

void OLED_clear(OLED_Bus bus){ memset(fb,0,sizeof(fb)); } // clear RAM framebuffer
void OLED_setCursor(uint8_t c,uint8_t r){cur_col=c;cur_row=r;} // move text cursor to 6 pixel column, 8 pixel row


// 6x8 font: space, digit, letters, colon where each char is 6 byte wide x 8 bits tall
static const uint8_t font6x8[][6]={
	
	{0x00,0x00,0x00,0x00,0x00,0x00}, // space
	{0x3E,0x51,0x49,0x45,0x3E,0x00}, // 0
	{0x00,0x42,0x7F,0x40,0x00,0x00}, // 1
	{0x42,0x61,0x51,0x49,0x46,0x00}, // 2
	{0x21,0x41,0x45,0x4B,0x31,0x00}, // 3
	{0x18,0x14,0x12,0x7F,0x10,0x00}, // 4
	{0x27,0x45,0x45,0x45,0x39,0x00}, // 5
	{0x3C,0x4A,0x49,0x49,0x30,0x00}, // 6
	{0x01,0x71,0x09,0x05,0x03,0x00}, // 7
	{0x36,0x49,0x49,0x49,0x36,0x00}, // 8
	{0x06,0x49,0x49,0x29,0x1E,0x00}, // 9
	{0x7E,0x09,0x09,0x09,0x7E,0x00}, // A
	{0x7F,0x49,0x49,0x49,0x36,0x00}, // B
	{0x3E,0x41,0x41,0x41,0x22,0x00}, // C
	{0x7F,0x41,0x41,0x22,0x1C,0x00}, // D
	{0x7F,0x49,0x49,0x49,0x41,0x00}, // E
	{0x7F,0x09,0x09,0x09,0x01,0x00}, // F
	{0x3E,0x41,0x49,0x49,0x7A,0x00}, // G
	{0x7F,0x08,0x08,0x08,0x7F,0x00}, // H
	{0x00,0x41,0x7F,0x41,0x00,0x00}, // I
	{0x20,0x40,0x41,0x3F,0x01,0x00}, // J
	{0x7F,0x08,0x14,0x22,0x41,0x00}, // K
	{0x7F,0x40,0x40,0x40,0x40,0x00}, // L
	{0x7F,0x02,0x0C,0x02,0x7F,0x00}, // M
	{0x7F,0x04,0x08,0x10,0x7F,0x00}, // N
	{0x3E,0x41,0x41,0x41,0x3E,0x00}, // O
	{0x7F,0x09,0x09,0x09,0x06,0x00}, // P
	{0x3E,0x41,0x51,0x21,0x5E,0x00}, // Q
	{0x7F,0x09,0x19,0x29,0x46,0x00}, // R
	{0x46,0x49,0x49,0x49,0x31,0x00}, // S
	{0x01,0x01,0x7F,0x01,0x01,0x00}, // T
	{0x3F,0x40,0x40,0x40,0x3F,0x00}, // U
	{0x1F,0x20,0x40,0x20,0x1F,0x00}, // V
	{0x3F,0x40,0x38,0x40,0x3F,0x00}, // W
	{0x63,0x14,0x08,0x14,0x63,0x00}, // X
	{0x07,0x08,0x70,0x08,0x07,0x00}, // Y
	{0x61,0x51,0x49,0x45,0x43,0x00}, // Z
	{0x00,0x36,0x36,0x00,0x00,0x00} // colon
};

// write one character into framebuffer at current 6x8 cursor, advance column
void OLED_putc(char c){
	if(c==' ') {cur_col++;return;} // quick space
	uint8_t idx=0; // default to blank
	if(c>='0'&&c<='9') idx=1+(c-'0'); // digit map to 1 - 10
	else if(c>='A'&&c<='Z') idx=11+(c-'A'); // letter map to 11 - 36 
	else if(c==':') idx=37; // colon
	else idx=0; // else is space
	uint16_t base=cur_row*OLED_WIDTH + cur_col*6; // byte offset in framebuffer
	if(base+6>=sizeof(fb)) return; // guard against overflow
	memcpy(&fb[base],font6x8[idx],6); // bit glyph to framebuffer
	cur_col++; // move to next cell
}

// print C string using OLED_putc
void OLED_print(const char *s){while(*s)OLED_putc(*s++);}

// send framebuffer to display over selected bus
void OLED_update(OLED_Bus bus){
	TWI_t *twi = (bus==BUS_TWI0)? &TWI0 : &TWI1;
	for(uint8_t p=0;p<OLED_PAGES;p++){
		OLED_write_cmd(bus,0xB0|p); // set page address and col 0. page p
		OLED_write_cmd(bus,0x00); // lower column nibble
		OLED_write_cmd(bus,0x10); // upper col nibble
		twi->MADDR = (OLED_ADDR<<1); // begin data stream
		while(!(twi->MSTATUS & TWI_WIF_bm)); // start + write
		twi->MDATA = 0x40; // data stream marker
		while(!(twi->MSTATUS & TWI_WIF_bm));
		
		// send 128 bytes for this page
		for(uint8_t i=0;i<OLED_WIDTH;i++){
			twi->MDATA = fb[p*OLED_WIDTH+i];
			while(!(twi->MSTATUS & TWI_WIF_bm));
		}
		twi->MCTRLB = TWI_MCMD_STOP_gc; // stop after each page
	}
}

// intialize one OLED on requested bus and clear the screen
void OLED_init(OLED_Bus bus){
	if(bus==BUS_TWI0) TWI0_init(); else TWI1_init(); // setup bus
	_delay_ms(100); 
	uint8_t cmds[]={0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0x00,0x40, // standard SSD1306 cmds like display off, display on etc
		0x8D,0x14,0x20,0x00,0xA1,0xC8,0xDA,0x12,
	0x81,0x7F,0xD9,0xF1,0xDB,0x40,0xA4,0xA6,0xAF};
	for(uint8_t i=0;i<sizeof(cmds);i++) OLED_write_cmd(bus,cmds[i]);
	OLED_clear(bus); // blank ram
	OLED_update(bus); // push clear to panel
}


// draw two text line tthen push to given bus 
void OLED_ShowPlayer(OLED_Bus bus,uint8_t player,
const char *card1,const char *card2,uint16_t balance){
	OLED_clear(bus);
	OLED_setCursor(0,0);
	char l1[20];
	sprintf(l1,"PLAYER %u:%s %s",player+1,card1,card2);
	OLED_print(l1);
	OLED_setCursor(0,2);
	char l2[20];
	sprintf(l2,"BALANCE:%u",balance);
	OLED_print(l2);
	OLED_update(bus);
}

