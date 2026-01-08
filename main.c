/*
 * main.c
 *
 * Created: 11/19/2025 3:12:55 PM
 * Author : ziyan mei
 */ 

#define F_CPU 16000000UL                   
#include <avr/io.h>                       
#include <util/delay.h>                    
#include <stdio.h>                         
#include <stdlib.h>                     
#include <string.h>                     
#include <avr/interrupt.h>           

#include "SPI.h"                           // MAX7219 7-segment matrix
#include "uart.h"                         
#include "OLED.h"                          // OLED_ShowPlayer, OLED_init

// Game constants
#define START_BALANCE 4000
#define SMALL_BLIND   25
#define BIG_BLIND     50
#define NUM_PLAYERS   2

// Buttons on PORTB (active-low with pullups)
#define BTN_ALLIN   PIN2_bm
#define BTN_FOLD    PIN3_bm
#define BTN_CALL    PIN4_bm
#define BTN_RAISE   PIN5_bm
#define BTN_MASK   (BTN_ALLIN|BTN_FOLD|BTN_CALL|BTN_RAISE)

/* ---------------- Software timers driven by the 1 ms TCA0 tick ---------------- */
volatile uint16_t t_adc = 0;               // cadence for reading pot and updating MAX7219
volatile uint16_t t_btn = 0;               // debounce cadence

/* ---------------- Non-blocking buzzer and LED flash engine state --------------- */
static volatile uint16_t buzz_time_ms = 0; // remaining duration of buzz
static volatile uint16_t buzz_half_period = 0; // half period in ms for square wave
static volatile uint16_t buzz_phase = 0;   // current countdown to toggle PD6

static volatile uint16_t led_period_ms = 0;        // period between LED toggles
static volatile uint16_t led_phase = 0;            // current countdown to toggle LEDs
static volatile uint16_t led_toggles_remaining = 0;// how many toggles left

/* ---------------- Heartbeat LED control on PC4 --------------------------------- */
volatile uint8_t game_active = 0;          // main sets 1 while a hand is running
static volatile uint16_t hb_div = 0;       // divides 1 ms to about 2 Hz blink

/* ---------------- Turn timeout in seconds, decremented by RTC OVF -------------- */
volatile uint16_t turn_timeout_s = 0;

/* ---------------- Last UART character captured by USART3 RX ISR ---------------- */
static volatile char g_uart_last = 0;

/* ============================== INTERRUPTS ===================================== */

// 1) TCA0 1 ms system tick: software timers, effects, heartbeat
ISR(TCA0_OVF_vect) {
	if (t_adc) t_adc--;                    // tick down ADC task timer
	if (t_btn) t_btn--;                    // tick down button debounce timer

	// Buzzer on PD6: simple square wave using half-period counter
	if (buzz_time_ms) {                    // if buzzing is active
		if (buzz_phase) buzz_phase--;      // count down to next edge
		else {                             // time to toggle
			PORTD.OUTTGL = PIN6_bm;        // toggle PD6 output
			buzz_phase = buzz_half_period; // reload half-period
		}
		if (--buzz_time_ms == 0)           // when duration expires
		PORTD.OUTCLR = PIN6_bm;        // ensure buzzer is low
	}

	// LED flash pattern on PD0..PD3
	if (led_toggles_remaining) {           // any toggles left
		if (led_phase) led_phase--;        // count down to next toggle
		else {
			PORTD.OUTTGL = PIN0_bm|PIN1_bm|PIN2_bm|PIN3_bm; // toggle LEDs
			led_phase = led_period_ms;     // reload period
			if (led_toggles_remaining) led_toggles_remaining--; // consume one toggle
			if (!led_toggles_remaining)    // finishing the pattern
			PORTD.OUTCLR = PIN0_bm|PIN1_bm|PIN2_bm|PIN3_bm; // LEDs off
		}
	}

	// Heartbeat LED on PC4: blink only while a hand is in progress
	if (game_active) {
		if (++hb_div >= 250) {             // ~250 ms half-period ? ~2 Hz blink
			PORTC.OUTTGL = PIN4_bm;        // toggle PC4
			hb_div = 0;                    // restart divider
		}
		} else {
		PORTC.OUTCLR = PIN4_bm;            // heartbeat off when idle
		hb_div = 0;                        // reset divider
	}

	TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm; // clear overflow interrupt flag
}

// 2) RTC counter overflow at 1 Hz: decrements the per-turn timeout
ISR(RTC_CNT_vect) {
	if (turn_timeout_s) turn_timeout_s--;  // one second elapsed
	RTC.INTFLAGS = RTC_OVF_bm;             // clear overflow flag
}

// 3) USART3 RX complete: capture one byte immediately, no Enter required
ISR(USART3_RXC_vect) {
	g_uart_last = USART3.RXDATAL;          // stash last char for buy-in prompt
}

/* ============================== PERIPHERALS ==================================== */

// Switch main clock to 16 MHz internal oscillator
static void CLOCK_init(void) {
	CPU_CCP = CCP_IOREG_gc;                // unlock protected clock register
	CLKCTRL.MCLKCTRLA = CLKCTRL_CLKSEL_OSCHF_gc; // select high-freq oscillator
	CPU_CCP = CCP_IOREG_gc;                // unlock again to change OSC freq select
	CLKCTRL.OSCHFCTRLA = CLKCTRL_FRQSEL_16M_gc;  // choose 16 MHz
	while (CLKCTRL.MCLKSTATUS & CLKCTRL_SOSC_bm); // wait if switching status busy
}

// TCA0 1 ms periodic interrupt
static void TIMER_init(void) {
	TCA0.SINGLE.CTRLB   = TCA_SINGLE_WGMODE_NORMAL_gc;            // normal mode
	TCA0.SINGLE.PER     = 249;                                    // 250 kHz ? 1 ms
	TCA0.SINGLE.INTCTRL = TCA_SINGLE_OVF_bm;                      // enable OVF IRQ
	TCA0.SINGLE.CTRLA   = TCA_SINGLE_CLKSEL_DIV64_gc | TCA_SINGLE_ENABLE_bm; // start
}

// RTC counter overflow every 1 second using 32.768 kHz internal OSC
static void RTC_init_overflow_1Hz(void){
	RTC.CLKSEL = RTC_CLKSEL_OSC32K_gc;     // select 32.768 kHz clock
	RTC.CTRLA = 0;                         // disable RTC while configuring
	while (RTC.STATUS & (RTC_PERBUSY_bm | RTC_CNTBUSY_bm | RTC_CTRLABUSY_bm)) {;} // wait ready
	RTC.CNT = 0;                           // start count at zero
	RTC.PER = 32767;                       // overflow after 32768 ticks = 1 s
	RTC.INTFLAGS = RTC_OVF_bm;             // clear any pending OVF
	RTC.INTCTRL  = RTC_OVF_bm;             // enable OVF interrupt
	RTC.CTRLA = RTC_RTCEN_bm | RTC_PRESCALER_DIV1_gc; // enable, prescaler 1
	while (RTC.STATUS & RTC_CTRLABUSY_bm) {;} // wait control sync
}

// Configure LEDs and buzzer pins
static void LED_BUZZ_Init(void){
	PORTC.DIRSET = PIN4_bm;                // PC4 as output for heartbeat LED
	PORTD.DIRSET = PIN0_bm|PIN1_bm|PIN2_bm|PIN3_bm|PIN6_bm; // PD0..3 LEDs, PD6 buzzer
	PORTC.OUTCLR = PIN4_bm;                // heartbeat off initially
	PORTD.OUTCLR = PIN0_bm|PIN1_bm|PIN2_bm|PIN3_bm|PIN6_bm; // LEDs and buzzer low
}

// Start a non-blocking buzz for duration_ms at freq_hz
static inline void BUZZ_Start(uint16_t duration_ms, uint16_t freq_hz){
	if (freq_hz < 50) freq_hz = 50;        // clamp frequency range
	if (freq_hz > 4000) freq_hz = 4000;
	uint16_t half_ms = (uint16_t)(500UL / freq_hz); // half-period in ms
	if (half_ms == 0) half_ms = 1;
	buzz_time_ms     = duration_ms;        // total remaining time
	buzz_half_period = half_ms;            // reload value for square-wave toggle
	buzz_phase       = 0;                  // toggle immediately on next tick
	PORTD.OUTCLR = PIN6_bm;                // ensure buzzer starts from low
}

// Start a non-blocking LED flash sequence on PD0..PD3
static inline void LEDs_FlashPattern(uint16_t toggles, uint16_t period_ms){
	if (period_ms == 0) period_ms = 1;     // avoid zero
	led_toggles_remaining = toggles;       // number of toggles to perform
	led_period_ms = period_ms;             // interval between toggles
	led_phase = 0;                          // toggle on next tick
	PORTD.OUTCLR = PIN0_bm|PIN1_bm|PIN2_bm|PIN3_bm; // start from LEDs off
}

// Configure button pins with pull-ups
static void BUTTONS_Init(void){
	PORTB.DIRCLR = BTN_MASK;               // inputs
	PORTB.PIN2CTRL = PORT_PULLUPEN_bm;     // enable pullups
	PORTB.PIN3CTRL = PORT_PULLUPEN_bm;
	PORTB.PIN4CTRL = PORT_PULLUPEN_bm;
	PORTB.PIN5CTRL = PORT_PULLUPEN_bm;
}

// Debounced button state container
typedef struct {
	uint8_t stable;                         // current sampled PORTB
	uint8_t last_stable;                    // previous sampled PORTB
	uint8_t pressed_edges;                  // rising press edges this slice
} button_state_t;

static button_state_t buttons;              // global debounced buttons

// Debounce task: detect press edges on active-low buttons
static void ButtonsTask(void){
	buttons.last_stable = buttons.stable;              // keep prior sample
	buttons.stable = PORTB.IN;                          // read current pins
	buttons.pressed_edges = (buttons.last_stable & BTN_MASK) & ~(buttons.stable & BTN_MASK);
	// pressed_edges bit is 1 only when going from 1 to 0 on an active-low button
}

// ADC set-up to read pot on ADC0 AIN8 (adjust to your board)
static void ADC_Init(void){
	VREF.ADC0REF = VREF_REFSEL_VDD_gc;      // reference = VDD
	ADC0.CTRLC   = ADC_PRESC_DIV4_gc;       // reasonable prescale for 16 MHz
	ADC0.MUXPOS  = ADC_MUXPOS_AIN8_gc;      // select input channel
	ADC0.CTRLA   = ADC_ENABLE_bm;           // enable ADC
}

// Single blocking ADC conversion
static uint16_t ADC_ReadOnce(void){
	ADC0.COMMAND = ADC_STCONV_bm;           // start conversion
	while(!(ADC0.INTFLAGS & ADC_RESRDY_bm));// wait for result
	ADC0.INTFLAGS = ADC_RESRDY_bm;          // clear flag
	return ADC0.RES;                         // return raw 10-bit result
}

// Smoothed ADC using IIR filter and small hysteresis
static uint16_t ADC_ReadSmooth(void){
	static uint32_t filt=0;                  // 8x scaled filter accumulator
	static uint16_t last=0;                  // last reported value for hysteresis
	uint32_t sum=0;
	for(uint8_t i=0;i<4;i++) sum += ADC_ReadOnce();      // 4-sample average
	uint16_t s = (uint16_t)(sum>>2);
	if(!filt) filt=((uint32_t)s)<<3;        // initialize filter
	filt = (filt*7 + ((uint32_t)s<<3))/8;   // 7/8 old + 1/8 new
	uint16_t o = (uint16_t)(filt>>3);       // back to 10-bit scale
	if(o>last+10) last=o;                   // 10-LSB deadband to reduce jitter
	else if(o+10<last) last=o;
	return last;
}

// MAX7219 function provided by your SPI driver
extern void Matrix_DisplayNumber(uint16_t n);

// Track current pot reading to show a raise amount
static uint16_t g_adc = 0;
static uint16_t g_bet_preview = 0;

// Update bet preview based on pot and player balance
static void BetAdjust(uint16_t max_bet, uint16_t player_balance){
	g_adc = ADC_ReadSmooth();                                   // read pot
	uint32_t scaled = ((uint32_t)g_adc * max_bet)/1023UL;       // scale 0..1023 to 0..max
	if (scaled > max_bet) scaled = max_bet;                     // clamp
	if (scaled > player_balance) scaled = player_balance;       // cannot exceed balance
	uint16_t bet = (uint16_t)scaled;
	if (bet != g_bet_preview){                                  // update only on change
		g_bet_preview = bet;
		Matrix_DisplayNumber(bet);                               // show on 7-segment
	}
}

/* ============================== POKER ENGINE =================================== */

// Deck and evaluator state
static char deck[52][3];                  // card strings like "AS", "TD"
static uint8_t deck_top = 0;              // index of next card to deal
static const char *ranks="23456789TJQKA"; // ranks low..high
static const char *suits="SHDC";          // Spades Hearts Diamonds Clubs

// Create, shuffle, and reset deck_top
static void init_deck(void){
	uint8_t k=0;
	for(uint8_t s=0;s<4;s++)              // for each suit
	for(uint8_t r=0;r<13;r++){            // for each rank
		deck[k][0]=ranks[r];              // set rank char
		deck[k][1]=suits[s];              // set suit char
		deck[k][2]='\0';                  // C-string terminator
		k++;
	}
	for(int i=51;i>0;i--){                // Fisher-Yates shuffle
		int j = rand()%(i+1);
		char t0=deck[i][0], t1=deck[i][1];
		deck[i][0]=deck[j][0]; deck[i][1]=deck[j][1];
		deck[j][0]=t0; deck[j][1]=t1;
	}
	deck_top=0;                            // reset pointer
}

// Copy next card text into dest
static void deal_card(char *dest){ if(deck_top<52) strcpy(dest,deck[deck_top++]); }

// Hand score helper
typedef struct { uint8_t value; uint8_t tiebreak[5]; } handscore_t;
static const char *handnames[]={
	"Invalid","High Card","One Pair","Two Pair","Three of a Kind",
	"Straight","Flush","Full House","Four of a Kind","Straight Flush"
};

// Convert rank char to numeric value 2..14
static uint8_t rv(char r){
	if(r>='2'&&r<='9')return r-'0';
	switch(r){case 'T':return 10;case 'J':return 11;case 'Q':return 12;case 'K':return 13;case 'A':return 14;}
	return 0;
}

// Sort small array descending
static void sort_desc(uint8_t *a,uint8_t n){
	for(uint8_t i=0;i<n-1;i++)
	for(uint8_t j=i+1;j<n;j++)
	if(a[j]>a[i]){uint8_t t=a[i];a[i]=a[j];a[j]=t;}
}

// 7-card evaluation (simple, fast enough for embedded demo)
static handscore_t evaluate7(char c[7][3]){
	uint8_t v[7],s[7];
	for(uint8_t i=0;i<7;i++){ v[i]=rv(c[i][0]); s[i]= (c[i][1]=='S')?0:(c[i][1]=='H')?1:(c[i][1]=='D')?2:3; }
	uint8_t cnt[15]={0}; for(uint8_t i=0;i<7;i++) cnt[v[i]]++;

	int flush=-1; for(uint8_t su=0;su<4;su++){uint8_t c2=0; for(uint8_t i=0;i<7;i++) if(s[i]==su) c2++; if(c2>=5){flush=su;break;}}
	uint8_t sortv[7]; for(uint8_t i=0;i<7;i++) sortv[i]=v[i]; sort_desc(sortv,7);

	int straight=0,seq=1;
	for(int i=0;i<6;i++){ if(sortv[i]==sortv[i+1])continue; if(sortv[i]==sortv[i+1]+1){ if(++seq>=5){straight=sortv[i-3];break;} } else seq=1; }
	if(!straight&&sortv[0]==14){uint8_t a2=0,a3=0,a4=0,a5=0; for(uint8_t i=0;i<7;i++){ if(sortv[i]==2)a2=1; if(sortv[i]==3)a3=1; if(sortv[i]==4)a4=1; if(sortv[i]==5)a5=1; } if(a2&&a3&&a4&&a5) straight=5;}

	handscore_t h={0,{0}};
	if(flush!=-1){uint8_t fv[7],n=0; for(uint8_t i=0;i<7;i++) if(s[i]==flush) fv[n++]=v[i]; sort_desc(fv,n); int sh=0,seq2=1;
		for(int i=0;i<n-1;i++){ if(fv[i]==fv[i+1])continue; if(fv[i]==fv[i+1]+1){ if(++seq2>=5){sh=fv[i-3];break;} } else seq2=1; }
		if(!sh&&fv[0]==14&&fv[n-1]==5) sh=5; if(sh){ h.value=9; h.tiebreak[0]=sh; return h; } }
		for(int v2=14;v2>=2;v2--) if(cnt[v2]==4){ h.value=8; h.tiebreak[0]=v2; return h; }
		int tri=0,pair=0; for(int v2=14;v2>=2;v2--) if(cnt[v2]>=3&&!tri) tri=v2; for(int v2=14;v2>=2;v2--) if(cnt[v2]>=2&&v2!=tri){pair=v2;break;}
		if(tri&&pair){ h.value=7; h.tiebreak[0]=tri; h.tiebreak[1]=pair; return h; }
		if(flush!=-1){ h.value=6; uint8_t fv[7],n=0; for(uint8_t i=0;i<7;i++) if(s[i]==flush) fv[n++]=v[i]; sort_desc(fv,n); for(uint8_t i=0;i<5;i++) h.tiebreak[i]=fv[i]; return h; }
		if(straight){ h.value=5; h.tiebreak[0]=straight; return h; }
		if(tri){ h.value=4; h.tiebreak[0]=tri; return h; }
		int p1=0,p2=0; for(int v2=14;v2>=2;v2--) if(cnt[v2]>=2){ if(!p1) p1=v2; else if(!p2){ p2=v2; break; } }
		if(p1&&p2){ h.value=3; h.tiebreak[0]=p1; h.tiebreak[1]=p2; return h; }
		if(p1){ h.value=2; h.tiebreak[0]=p1; return h; }
		h.value=1; for(uint8_t i=0;i<5;i++) h.tiebreak[i]=sortv[i]; return h;
	}

	// Compare two hand scores
	static int compare_hands(handscore_t a,handscore_t b){
		if(a.value>b.value) return 1;
		if(a.value<b.value) return -1;
		for(uint8_t i=0;i<5;i++){ if(a.tiebreak[i]>b.tiebreak[i]) return 1; if(a.tiebreak[i]<b.tiebreak[i]) return -1; }
		return 0;
	}

	// OLED hook to draw a player snapshot
	extern void OLED_ShowPlayer(uint8_t bus, uint8_t p, const char *c0, const char *c1, uint16_t bal);

	/* ---------------- Game state machine ---------------- */
	typedef enum {
		GS_IDLE=0, GS_WAIT_BUYIN, GS_NEW_HAND,
		GS_PREFLOP, GS_FLOP, GS_TURN, GS_RIVER,
		GS_SHOWDOWN, GS_FOLDWIN
	} gstate_t;

	static gstate_t g_state = GS_IDLE;                 // current game state
	static uint8_t  dealer   = 0;                      // dealer toggles each hand
	static uint16_t bal[NUM_PLAYERS] = {START_BALANCE, START_BALANCE}; // chip counts
	static uint16_t pot=0, cur=0, last_raise=0, last_bet[2]={0,0};    // betting state
	static uint8_t  allin[2]={0,0}, foldp[2]={0,0};    // per-player flags
	static char     board[5][3];                       // community cards
	static char     handc[2][2][3];                    // two hole cards per player
	static uint8_t  actor=0;                            // whose turn is acting

	// buy-in prompt latches and per-street acted flags
	static uint8_t prompted_buyin[2] = {0,0};
	static uint8_t acted_since_raise[2] = {0,0};

	/* ---------------- Tasks and helpers ---------------- */

	// Non-blocking buy-in prompt handled via USART RX ISR byte
	static uint8_t BuyIn(uint8_t p){
		if (bal[p] > 0) { prompted_buyin[p] = 0; return 1; }   // already funded
		if (!prompted_buyin[p]){                               // print prompt once
			printf("\r\nPlayer %u is out of chips! Buy-in again for %u chips? (y/n): ", p+1, START_BALANCE);
			prompted_buyin[p] = 1;
		}
		if (g_uart_last=='y' || g_uart_last=='Y'){             // re-buy accepted
			g_uart_last = 0; bal[p] = START_BALANCE;
			printf("Player %u re-buys.\r\n", p+1);
			prompted_buyin[p] = 0; return 1;
		}
		if (g_uart_last=='n' || g_uart_last=='N'){             // decline
			g_uart_last = 0; prompted_buyin[p] = 0; return 0;
		}
		return 2;                                              // still waiting
	}

	// Restart the 1-second RTC phase and clear pending flags
	static void rtc_restart_1s(void){
		RTC.INTFLAGS = RTC_OVF_bm;
		RTC.CNT      = 0;
	}

	// Initialize a new hand, post blinds, deal cards, set timeout and actor
	static void StartNewHand(void){
		memset(allin,0,sizeof allin);
		memset(foldp,0,sizeof foldp);
		memset(last_bet,0,sizeof last_bet);
		acted_since_raise[0] = acted_since_raise[1] = 0;

		pot = 0; cur = BIG_BLIND; last_raise = BIG_BLIND;

		uint8_t sb = dealer, bb = 1 - dealer;  // dealer posts small blind this round
		dealer = 1 - dealer;                   // toggle dealer for next hand

		if (bal[sb] >= SMALL_BLIND){ bal[sb]-=SMALL_BLIND; last_bet[sb]=SMALL_BLIND; }
		else { last_bet[sb]=bal[sb]; bal[sb]=0; allin[sb]=1; }      // SB all-in short
		if (bal[bb] >= BIG_BLIND){ bal[bb]-=BIG_BLIND; last_bet[bb]=BIG_BLIND; }
		else { last_bet[bb]=bal[bb]; bal[bb]=0; allin[bb]=1; }      // BB all-in short
		pot = last_bet[sb] + last_bet[bb];

		printf("P%u posts %u (SB), P%u posts %u (BB) – Pot=%u\r\n", sb+1, SMALL_BLIND, bb+1, BIG_BLIND, pot);

		init_deck();                                 // shuffle deck

		for(uint8_t i=0;i<2;i++){                    // deal two hole cards each
			deal_card(handc[i][0]);
			deal_card(handc[i][1]);
			OLED_ShowPlayer(i==0?BUS_TWI0:BUS_TWI1, i, handc[i][0], handc[i][1], bal[i]);
		}

		for(uint8_t i=0;i<5;i++) deal_card(board[i]); // pre-draw all board cards

		actor = (1 - dealer);                        // first to act is left of dealer
		turn_timeout_s = 20;                         // 20 second turn timer
		rtc_restart_1s();                            // align RTC to now

		printf("\r\n=== PRE-FLOP ===\r\n");
	}

	// Reset street state, move to next street or showdown, restart timer
	static void Advance(void){
		last_bet[0] = last_bet[1] = 0;              // equalize current bets
		cur = 0; last_raise = BIG_BLIND;            // minimum raise baseline
		acted_since_raise[0] = acted_since_raise[1] = 0;
		actor = (1 - dealer);                        // first to act on each street
		turn_timeout_s = 20; rtc_restart_1s();      // reset turn timer

		if (g_state == GS_PREFLOP){
			g_state = GS_FLOP;
			printf("\r\n=== FLOP ===\r\n");
			printf("Community: %s %s %s\r\n", board[0], board[1], board[2]);
			} else if (g_state == GS_FLOP){
			g_state = GS_TURN;
			printf("\r\n=== TURN ===\r\n");
			printf("Community: %s %s %s %s\r\n", board[0], board[1], board[2], board[3]);
			} else if (g_state == GS_TURN){
			g_state = GS_RIVER;
			printf("\r\n=== RIVER ===\r\n");
			printf("Community: %s %s %s %s %s\r\n", board[0], board[1], board[2], board[3], board[4]);
			} else {
			g_state = GS_SHOWDOWN;
			printf("\r\n=== SHOWDOWN ===\r\n");
			printf("Community: %s %s %s %s %s\r\n", board[0], board[1], board[2], board[3], board[4]);
		}
	}

	// Handle one player’s turn in a non-blocking task style
	static void HandlePlayerTurnTask(void){
		uint8_t p = actor;                                       // acting player index

		// Skip if this player already all-in, folded, or broke
		if (allin[p] || foldp[p] || bal[p]==0){
			actor = 1 - p;                                       // pass turn
			turn_timeout_s = 20; rtc_restart_1s();               // reset timer for next
			return;
		}

		uint8_t e = buttons.pressed_edges;                       // read press edges

		// ALL-IN button
		if (e & BTN_ALLIN){
			pot += bal[p]; last_bet[p] += bal[p]; bal[p]=0; allin[p]=1;
			if (last_bet[p] > cur) cur = last_bet[p];
			printf("Player %u ALL IN! Pot=%u\r\n", p+1, pot);

			BUZZ_Start(220, 1000);                               // louder buzzer
			LEDs_FlashPattern(40, 60);                           // big flash
			OLED_ShowPlayer(p==0?BUS_TWI0:BUS_TWI1, p, handc[p][0], handc[p][1], bal[p]);

			acted_since_raise[p] = 1;
			actor = 1 - p; turn_timeout_s=20; rtc_restart_1s();
			return;
		}

		// FOLD button
		if (e & BTN_FOLD){
			foldp[p]=1;
			printf("Player %u folds.\r\n", p+1);
			acted_since_raise[p] = 1;
			actor = 1 - p; turn_timeout_s=20; rtc_restart_1s();
			return;
		}

		// CALL or CHECK button
		if (e & BTN_CALL){
			uint16_t need = (cur>last_bet[p]) ? (cur - last_bet[p]) : 0; // amount to call
			if (need > bal[p]) need = bal[p];                             // cap at stack
			pot += need; bal[p] -= need; last_bet[p] += need;
			if (last_bet[p] > cur) cur = last_bet[p];

			if (need==0) printf("Player %u checks.\r\n", p+1);
			else printf("Player %u calls %u. Pot=%u\r\n", p+1, need, pot);

			if (bal[p]==0){                                             // became all-in
				allin[p]=1; BUZZ_Start(180, 900); LEDs_FlashPattern(20, 60);
			}
			OLED_ShowPlayer(p==0?BUS_TWI0:BUS_TWI1, p, handc[p][0], handc[p][1], bal[p]);

			acted_since_raise[p] = 1;
			actor = 1 - p; turn_timeout_s=20; rtc_restart_1s();
			return;
		}

		// RAISE button
		if (e & BTN_RAISE){
			uint16_t raise = g_bet_preview;                          // from pot input
			uint16_t need  = (cur>last_bet[p]) ? (cur - last_bet[p]) : 0;

			if (bal[p] <= need){                                     // cannot cover call
				pot += bal[p]; last_bet[p]+=bal[p]; bal[p]=0; allin[p]=1;
				printf("Player %u goes ALL IN. Pot=%u\r\n", p+1, pot);
				BUZZ_Start(180, 1000); LEDs_FlashPattern(20, 60);
				} else {
				if (raise < last_raise) raise = last_raise;          // minimum raise rule
				uint16_t tot = need + raise;                         // call + raise
				if (tot > bal[p]) tot = bal[p];                      // all-in cap
				pot += tot; bal[p]-=tot; last_bet[p]+=tot;
				if (last_bet[p] > cur) cur = last_bet[p];
				last_raise = raise;                                  // update min raise
				printf("Player %u raises %u (Pot=%u)\r\n", p+1, tot, pot);
				if (bal[p]==0){ allin[p]=1; BUZZ_Start(180, 1000); LEDs_FlashPattern(20, 60); }
			}
			OLED_ShowPlayer(p==0?BUS_TWI0:BUS_TWI1, p, handc[p][0], handc[p][1], bal[p]);

			acted_since_raise[p]   = 1;                              // this player acted
			acted_since_raise[1-p] = 0;                              // other must respond
			actor = 1 - p; turn_timeout_s=20; rtc_restart_1s();
			return;
		}

		// No button press and the RTC-driven timeout reached zero
		if (!turn_timeout_s){
			uint16_t need = (cur>last_bet[p]) ? (cur - last_bet[p]) : 0;
			if (need==0) printf("Player %u auto-checks.\r\n", p+1);
			else if (need <= bal[p]){
				pot+=need; bal[p]-=need; last_bet[p]+=need;
				if (last_bet[p]>cur) cur = last_bet[p];
				printf("Player %u auto-calls %u.\r\n", p+1, need);
				} else {
				foldp[p]=1; printf("Player %u folds (timeout).\r\n", p+1);
			}
			if (bal[p]==0){ allin[p]=1; BUZZ_Start(140, 800); LEDs_FlashPattern(12, 60); }
			OLED_ShowPlayer(p==0?BUS_TWI0:BUS_TWI1, p, handc[p][0], handc[p][1], bal[p]);

			acted_since_raise[p] = 1;
			actor = 1 - p; turn_timeout_s=20; rtc_restart_1s();
		}
	}

	/* ============================== MAIN LOOP ====================================== */

	int main(void){
		CLOCK_init();                           // 16 MHz clock
		uart_init(3,9600,NULL);                 // UART3 at 9600 for printf
		SPI_Init();                             // SPI for MAX7219
		ADC_Init();                             // ADC for potentiometer
		BUTTONS_Init();                         // buttons with pullups
		LED_BUZZ_Init();                        // LEDs and buzzer GPIO
		TIMER_init();                           // TCA0 1 ms tick
		RTC_init_overflow_1Hz();                // RTC 1 Hz overflow

		USART3.CTRLA |= USART_RXCIE_bm;         // enable RX complete interrupt
		USART3.CTRLB |= USART_RXEN_bm;          // enable receiver

		sei();                                   // global interrupts on

		srand(1);                                // deterministic shuffle for testing
		OLED_init(BUS_TWI0);                     // left OLED
		OLED_init(BUS_TWI1);                     // right OLED

		t_adc = 1;                               // schedule initial tasks soon
		t_btn = 1;

		g_state = GS_WAIT_BUYIN;                 // start at buy-in gate
		dealer  = 0;                             // player 1 is initial dealer

		while(1){
			// Cooperative periodic tasks
			if(!t_btn){ t_btn = 10; ButtonsTask(); }                // debounce every 10 ms
			if(!t_adc){ t_adc = 20; BetAdjust(4000, bal[actor]); }  // preview every 20 ms

			// Tell the ISR whether to blink the heartbeat LED
			switch(g_state){
				case GS_PREFLOP: case GS_FLOP: case GS_TURN:
				case GS_RIVER: case GS_SHOWDOWN: case GS_FOLDWIN:
				case GS_NEW_HAND: game_active = 1; break;
				default:          game_active = 0; break;
			}

			// State machine
			switch(g_state){
				case GS_WAIT_BUYIN: {
					uint8_t s0 = BuyIn(0);                  // query player 1
					uint8_t s1 = BuyIn(1);                  // query player 2
					if (s0==2 || s1==2) break;              // still waiting for input

					if (s0==0 || s1==0){                    // someone declined re-buy
						uint8_t winner;
						if (bal[0] > bal[1]) winner = 0;
						else if (bal[1] > bal[0]) winner = 1;
						else winner = 255;

						if (winner != 255){
							printf("\r\nGAME OVER\r\n");
							printf("Player %u wins with %u chips\r\n", winner+1, bal[winner]);
							} else {
							printf("\r\nGAME OVER\r\n");
							printf("Tie game. Both players at %u chips\r\n", bal[0]);
						}

						bal[0] = START_BALANCE;             // reset stacks
						bal[1] = START_BALANCE;
						prompted_buyin[0] = prompted_buyin[1] = 0;
						g_uart_last = 0;

						printf("\r\n=== NEW GAME ===\r\n");
						g_state = GS_NEW_HAND;              // immediately spin new hand
						break;
					}

					g_state = GS_NEW_HAND;                  // both funded: start hand
				} break;

				case GS_NEW_HAND:
				StartNewHand();                         // posts blinds, deals, timer
				g_state = GS_PREFLOP;                   // move to first betting street
				break;

				case GS_PREFLOP:
				case GS_FLOP:
				case GS_TURN:
				case GS_RIVER: {
					if (foldp[0]^foldp[1]) {               // exactly one player folded
						g_state = GS_FOLDWIN;
						break;
					}

					// If both are all-in, auto-reveal remaining streets
					if (allin[0] && allin[1]){
						if (g_state == GS_PREFLOP){
							g_state = GS_FLOP;
							printf("\r\n=== FLOP ===\r\n");
							printf("Community: %s %s %s\r\n", board[0],board[1],board[2]);
							} else if (g_state == GS_FLOP){
							g_state = GS_TURN;
							printf("\r\n=== TURN ===\r\n");
							printf("Community: %s %s %s %s\r\n", board[0],board[1],board[2],board[3]);
							} else if (g_state == GS_TURN){
							g_state = GS_RIVER;
							printf("\r\n=== RIVER ===\r\n");
							printf("Community: %s %s %s %s %s\r\n", board[0],board[1],board[2],board[3],board[4]);
							} else {
							g_state = GS_SHOWDOWN;
							printf("\r\n=== SHOWDOWN ===\r\n");
							printf("Community: %s %s %s %s %s\r\n", board[0],board[1],board[2],board[3],board[4]);
						}
						break;
					}

					HandlePlayerTurnTask();                // process buttons or timeout

					uint8_t both_acted = acted_since_raise[0] && acted_since_raise[1];
					uint8_t bets_equal = (last_bet[0] == last_bet[1]);
					if (both_acted && bets_equal){        // street complete
						Advance();
					}
				} break;

				case GS_SHOWDOWN: {
					// Build 7-card arrays per player: 2 hole + 5 board
					char f0[7][3], f1[7][3];
					strcpy(f0[0],handc[0][0]); strcpy(f0[1],handc[0][1]);
					strcpy(f1[0],handc[1][0]); strcpy(f1[1],handc[1][1]);
					for(uint8_t j=0;j<5;j++){ strcpy(f0[j+2],board[j]); strcpy(f1[j+2],board[j]); }

					handscore_t s0 = evaluate7(f0), s1 = evaluate7(f1); // evaluate
					printf("Player 1: %s\r\n", handnames[s0.value]);
					printf("Player 2: %s\r\n", handnames[s1.value]);

					uint16_t pot_p0 = 0, pot_p1 = 0;
					uint16_t b0_before = bal[0], b1_before = bal[1];
					int cmp = compare_hands(s0,s1);
					if (cmp>0){
						pot_p0 = pot; bal[0] += pot_p0;
						printf("Player 1 wins +%u chips  (P1 %u -> %u)\r\n", pot_p0, b0_before, bal[0]);
						} else if (cmp<0){
						pot_p1 = pot; bal[1] += pot_p1;
						printf("Player 2 wins +%u chips  (P2 %u -> %u)\r\n", pot_p1, b1_before, bal[1]);
						} else {
						pot_p0 = pot/2; pot_p1 = pot - pot_p0; // odd chip to P2
						bal[0] += pot_p0; bal[1] += pot_p1;
						printf("Split pot: P1 +%u ( %u -> %u ), P2 +%u ( %u -> %u )\r\n",
						pot_p0, b0_before, bal[0], pot_p1, b1_before, bal[1]);
					}
					pot=0;

					OLED_ShowPlayer(BUS_TWI0,0,handc[0][0],handc[0][1],bal[0]); // refresh OLEDs
					OLED_ShowPlayer(BUS_TWI1,1,handc[1][0],handc[1][1],bal[1]);

					printf("Balances now: P1=%u  P2=%u\r\n", bal[0], bal[1]);
					g_state = GS_WAIT_BUYIN;               // go to buy-in gate
				} break;

				case GS_FOLDWIN: {
					uint8_t w = foldp[0]?1:0;              // winner index by fold
					uint16_t b0_before = bal[0], b1_before = bal[1];
					uint16_t award = pot;                  // winner takes all

					if (w == 0){
						bal[0] += award;
						printf("Player 1 wins by fold +%u  (P1 %u -> %u)\r\n", award, b0_before, bal[0]);
						} else {
						bal[1] += award;
						printf("Player 2 wins by fold +%u  (P2 %u -> %u)\r\n", award, b1_before, bal[1]);
					}
					pot=0;

					OLED_ShowPlayer(BUS_TWI0,0,handc[0][0],handc[0][1],bal[0]);
					OLED_ShowPlayer(BUS_TWI1,1,handc[1][0],handc[1][1],bal[1]);

					printf("Balances now: P1=%u  P2=%u\r\n", bal[0], bal[1]);
					g_state = GS_WAIT_BUYIN;               // go to buy-in gate
				} break;

				default: break;                            // should not happen
			}
		}
	}
