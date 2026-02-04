/*
 * Poker.c
 *
 * Created: 11/29/2025 12:23:26 AM
 * Author : heton
 */ 

#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdlib.h>
#include "uart.h"
#include "card.h"

//ISR Memory
volatile uint16_t ticks = 0;
volatile uint8_t usartRXChar = 0;
volatile uint8_t usartRXReady = 0;
volatile uint8_t showdown = 0;

static inline void clockInit(void){
	CPU_CCP = CCP_IOREG_gc;
	CLKCTRL.MCLKCTRLA = CLKCTRL_CLKSEL_OSCHF_gc;
	CPU_CCP = CCP_IOREG_gc;
	CLKCTRL.OSCHFCTRLA = CLKCTRL_FRQSEL_16M_gc;
	CPU_CCP = CCP_IOREG_gc;
}

static void timerInit(){
	TCA0.SINGLE.CTRLB = TCA_SINGLE_WGMODE_NORMAL_gc;
	TCA0.SINGLE.PER = 249;//For 16Mhz clock, this is start period for 1ms period
	TCA0.SINGLE.INTCTRL = TCA_SINGLE_OVF_bm;
	TCA0.SINGLE.CTRLA |= (TCA_SINGLE_CLKSEL_DIV64_gc | TCA_SINGLE_ENABLE_bm);
}

ISR(TCA0_OVF_vect){
	ticks++;
	if((ticks > 10000)){
		ticks = 1;
	}
	TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;
}

ISR(USART3_RXC_vect){//For pausing and resuming
	uint8_t c = USART3.RXDATAL;
	usartRXChar = c;
	usartRXReady = 1; //Tells main loop a char is ready
	
	if (USART3.STATUS & USART_DREIF_bm) {
		//Prints what we type into the oRXState = 0;
		USART3.TXDATAL = c;
	}
	
}

uint8_t usartGetChar(uint8_t *out){
	if(usartRXReady == 0){
		return 0;
	}
	*out = usartRXChar;
	usartRXReady = 0;
	return 1;
}

void game_step(Game *g){
	uint8_t c; 
	switch (g->round){
		case ROUND_MENU:
			if(g->turn == 0){
				printf("\r\nPlay 2-Person poker? (y/n)");
				g->turn = 1;
			}
			else{
				if(usartGetChar(&c) == 0){
					return; //No key yet, return later
				}
				if(c == 'y'|| c== 'Y'){
					card_init();
					card_shuffle(ticks);
				
					g->pot            = 0;
					g->communityCount = 0;

					g->round = ROUND_FLOP;
					g->turn  = 0;
					showdown = 0;
				}
				else{
					printf("\r\nNot starting a game, press Y when ready.\r\n");
					g->turn = 0;
				}
			}
			break;
		case ROUND_FLOP:
			if(g->turn == 0){
				//Blinds
				g->p1.money -= 10;
				g->p1.currentBet  = 10;
				g->p2.money      -= 10;
				g->p2.currentBet  = 10;
				g->pot            = 20;
			
				g->p1.isActive = 1;
				g->p2.isActive = 1;
				
				g->p1.allIn = 0;
				g->p2.allIn = 0;
				
				deal_player_cards(g);
				deal_player_cards(g);
				//Print flop Cards
				char p1c1[8], p1c2[8];
				char p2c1[8], p2c2[8];

				card_toString(g->p1.card1, p1c1, sizeof(p1c1));
				card_toString(g->p1.card2, p1c2, sizeof(p1c2));
				card_toString(g->p2.card1, p2c1, sizeof(p2c1));
				card_toString(g->p2.card2, p2c2, sizeof(p2c2));

				printf("\r\nPlayer 1 cards: %s %s\r\n", p1c1, p1c2);
				printf("Player 2 cards: %s %s\r\n", p2c1, p2c2);
				
				deal_community(g, 3);
				char c1[8],c2[8],c3[8];
				card_toString(g->community[0],c1,sizeof(c1));
				card_toString(g->community[1],c2,sizeof(c2));
				card_toString(g->community[2],c3,sizeof(c3));
				
				printf("\r\nCommunity Cards 1, 2, 3: %s %s %s\r\n", c1, c2, c3);
				g->turn = 1;
				printf("Player 1 Round Flop, Options: Fold, Call, Raise(F/C/R)\n");
			}
			else if(g->turn == 1){
				if(g->p1.isActive == 0){//Already folded
					g->turn = 2;
					printf("Player 1 has already folded\n");
					printf("Player 2 Round Flop, Options: Fold, Call, Raise(F/C/R)\n");
					return;
				}
				if(usartGetChar(&c) == 0){
					return; //No key yet, return later
				}

				if(c == 'f' || c == 'F'){
					//Folded
					g->p1.isActive = 0;
					printf("Player 1 has folded\n");
					g->turn = 2;
					printf("Player 2 Round Flop, Options: Fold, Call, Raise(F/C/R)\n");
				}
				else if (c == 'c'|| c== 'C'){
					//Called (if player 2's current bet is higher, then that should be player 1's current bet, else bet stays the same
					printf("Player 1 has called\n");
					if(g->p2.currentBet > g->p1.currentBet){
						uint16_t difference = g->p2.currentBet - g->p1.currentBet;
						if (difference >= g->p1.money){//Go all in
							g->p1.currentBet += g->p1.money;
							g->p1.money = 0;
							g->p1.allIn = 1; 
							printf("Player 1 has gone all in!\n");
						}
						else{//Match p2's current bet
							g->p1.currentBet += difference;
							g->p1.money -= difference;
							printf("Player 1 has matched Player 2's bet. current Bet is: %u, current Money is: %u\n", g->p1.currentBet, g->p1.money);
						}
					}
					g->turn = 2;
					printf("Player 2 Turn Flop, Options: Fold, Call, Raise(F/C/R)\n");

				}
				else if (c == 'r' || c == 'R'){
					//Raise (flat 10 raise for now)
					printf("Player 1 has raised by 10\n");
					if(g->p1.money - 10 <= 0){ //All in
						g->p1.currentBet += g->p1.money;
						g->p1.money = 0;
						g->p1.allIn = 1;
						printf("Player 1 has gone all in!\n");
					}
					else{ //Simply raise by 10
						g->p1.money -= 10;
						g->p1.currentBet  += 10;
						printf("Player 1 has raised by 10, current Bet is: %u, current Money is: %u\n", g->p1.currentBet, g->p1.money);
					}
					
					g->turn = 2; //Go to player 2's turn
					printf("Player 2 Round Flop, Options: Fold, Call, Raise(F/C/R)\n");
					
				}
				
				else{
					printf("Invalid Key Entered\n");
				}
				g->pot = g->p1.currentBet + g->p2.currentBet; //Update bet
			}
			else if(g->turn == 2){
				if(g->p2.isActive == 0){//Already folded
					g->turn = 0;
					g->round = ROUND_TURN;
					printf("Player 2 has already folded\n");
					printf("Player 1 Round Turn, Options: Fold, Call, Raise(F/C/R)\n");
					return;
				}
				if(usartGetChar(&c) == 0){
					return; //No key yet, return later
				}
				if(c == 'f' || c == 'F'){
					//Folded
					g->p2.isActive = 0;
					printf("Player 2 has folded\n");
					g->turn = 0; //Should be next turn, USART
					g->round = ROUND_TURN;
				}
				else if (c == 'c'|| c== 'C'){
					//Called (if player 1's current bet is higher, then that should be player 2's current bet, else bet stays the same
					printf("Player 2 has called\n");
					if(g->p1.currentBet > g->p2.currentBet){
						uint16_t difference = g->p1.currentBet - g->p2.currentBet;
						if (difference >= g->p2.money){//Go all in
							g->p2.currentBet += g->p2.money;
							g->p2.money = 0;
							g->p2.allIn = 1;
							printf("Player 2 has gone all in!\n");
						}
						else{//Match p2's current bet
							g->p2.currentBet += difference;
							g->p2.money -= difference;
							printf("Player 2 has matched Player 1's bet. current Bet is: %u, current Money is: %u\n", g->p2.currentBet, g->p2.money);
						}
					}
					g->turn = 0; //Should be next turn, USART
					g->round = ROUND_TURN;
				}
				else if (c == 'r' || c == 'R'){
					//Raise (flat 10 raise for now), should go back to Player 1's turn for their action
					printf("Player 2 has raised by 10\n");
					if(g->p2.money - 10 <= 0){ //All in
						g->p2.currentBet += g->p2.money;
						g->p2.money = 0;
						g->p2.allIn = 1; 
						printf("Player 2 has gone all in!\n");
					}
					else{ //Simply raise by 10
						g->p2.money -= 10;
						g->p2.currentBet  += 10;
						printf("Player 2 has raised by 10, current Bet is: %u, current Money is: %u\n", g->p2.currentBet, g->p2.money);
					}
	
					g->turn = 1; //Go back to player 1's turn for their action
					printf("Player 1 Round Flop, Options: Fold, Call, Raise(F/C/R)\n");
				}
				else{
					printf("Invalid Key Entered\n");
				}
				g->pot = g->p1.currentBet + g->p2.currentBet; //Update bet
			}
			
			break;
		case ROUND_TURN:
			if(g->turn == 0){
				deal_community(g, 1); //Deal 4th community card

				char c1[8],c2[8],c3[8],c4[8];
				card_toString(g->community[0],c1,sizeof(c1));
				card_toString(g->community[1],c2,sizeof(c2));
				card_toString(g->community[2],c3,sizeof(c3));
				card_toString(g->community[3],c4,sizeof(c4));
								
				printf("\r\nCommunity Cards 1, 2, 3, 4: %s %s %s %s\r\n", c1, c2, c3, c4);
				printf("Pot: %u\r\n", g->pot);
				
				g->turn = 1;
				printf("Player 1 Round Turn, Options: Fold, Call, Raise(F/C/R)\n");
			}
			else if (g->turn == 1) {
				if (g->p1.isActive == 0) {
					g->turn = 2;
					printf("Player 1 has already folded\n");
					printf("Player 2 Round Turn, Options: Fold, Call, Raise(F/C/R)\n");
					return;
				}
				if (usartGetChar(&c) == 0) return;

				// EXACT SAME LOGIC AS FLOP (P1)
				if (c == 'f' || c == 'F') {
					g->p1.isActive = 0;
					printf("Player 1 has folded\n");
					g->turn = 2;
					printf("Player 2 Round Turn, Options: Fold, Call, Raise(F/C/R)\n");
					} 
				else if (c == 'c' || c == 'C') {
					printf("Player 1 has called\n");
					if (g->p2.currentBet > g->p1.currentBet) {
						uint16_t difference = g->p2.currentBet - g->p1.currentBet;
						if (difference >= g->p1.money) {
							g->p1.currentBet += g->p1.money;
							g->p1.money = 0;
							g->p1.allIn = 1;
							printf("Player 1 has gone all in!\n");
							} 
						else {
							g->p1.currentBet += difference;
							g->p1.money      -= difference;
							printf("Player 1 matched P2. Bet: %u, Money: %u\n",
							g->p1.currentBet, g->p1.money);
						}
					}
					g->turn = 2;
					printf("Player 2 Round Turn, Options: Fold, Call, Raise(F/C/R)\n");
				} 
				else if (c == 'r' || c == 'R') {
					printf("Player 1 has raised by 10\n");
					if (g->p1.money - 10 <= 0) {
						g->p1.currentBet += g->p1.money;
						g->p1.money = 0;
						g->p1.allIn = 1;
						printf("Player 1 has gone all in!\n");
						} else {
						g->p1.money      -= 10;
						g->p1.currentBet += 10;
						printf("Player 1 raised by 10. Bet: %u, Money: %u\n",
						g->p1.currentBet, g->p1.money);
					}
					g->turn = 2;
					printf("Player 2 Round Turn, Options: Fold, Call, Raise(F/C/R)\n");
					} 
				else {
					printf("Invalid key\n");
				}
				g->pot = g->p1.currentBet + g->p2.currentBet;
			}
			else if (g->turn == 2) {
				if (g->p2.isActive == 0) {
					g->turn  = 0;
					g->round = ROUND_RIVER;
					printf("Player 2 has already folded\n");
					printf("Player 1 Round River, Options: Fold, Call, Raise(F/C/R)\n");
					return;
				}
				if (usartGetChar(&c) == 0) return;

				// EXACT SAME LOGIC AS FLOP (P2)
				if (c == 'f' || c == 'F') {
					g->p2.isActive = 0;
					printf("Player 2 has folded\n");
					g->turn  = 0;
					g->round = ROUND_RIVER;
					} 
				else if (c == 'c' || c == 'C') {
					printf("Player 2 has called\n");
					if (g->p1.currentBet > g->p2.currentBet) {
						uint16_t difference = g->p1.currentBet - g->p2.currentBet;
						if (difference >= g->p2.money) {
							g->p2.currentBet += g->p2.money;
							g->p2.money = 0;
							g->p2.allIn = 1;
							printf("Player 2 has gone all in!\n");
							} 
						else {
							g->p2.currentBet += difference;
							g->p2.money      -= difference;
							printf("Player 2 matched P1. Bet: %u, Money: %u\n",
							g->p2.currentBet, g->p2.money);
						}
					}
					g->turn  = 0;
					g->round = ROUND_RIVER;
				} 
				else if (c == 'r' || c == 'R') {
					printf("Player 2 has raised by 10\n");
					if (g->p2.money - 10 <= 0) {
						g->p2.currentBet += g->p2.money;
						g->p2.money = 0;
						g->p2.allIn = 1;
						printf("Player 2 has gone all in!\n");
						} 
					else {
						g->p2.money      -= 10;
						g->p2.currentBet += 10;
						printf("Player 2 raised by 10. Bet: %u, Money: %u\n",
						g->p2.currentBet, g->p2.money);
					}
					g->turn = 1;
					printf("Player 1 Round Turn, Options: Fold, Call, Raise(F/C/R)\n");
				} 
				else {
					printf("Invalid key\n");
				}
				g->pot = g->p1.currentBet + g->p2.currentBet;
			}
			break;
		case ROUND_RIVER:
			if (g->turn == 0) {
				// Deal 5th community card
				deal_community(g, 1);
				char c1[8],c2[8],c3[8],c4[8],c5[8];
				card_toString(g->community[0],c1,sizeof(c1));
				card_toString(g->community[1],c2,sizeof(c2));
				card_toString(g->community[2],c3,sizeof(c3));
				card_toString(g->community[3],c4,sizeof(c4));
				card_toString(g->community[4],c5,sizeof(c5));
				
				printf("\r\nCommunity Cards 1, 2, 3, 4, 5: %s %s %s %s %s\r\n", c1, c2, c3, c4, c5);
				printf("Pot: %u\r\n", g->pot);

				g->turn = 1;
				printf("Player 1 Round River, Options: Fold, Call, Raise(F/C/R)\n");
			}
			else if (g->turn == 1) {
				if (g->p1.isActive == 0) {
					g->turn = 2;
					printf("Player 1 has already folded\n");
					printf("Player 2 Round River, Options: Fold, Call, Raise(F/C/R)\n");
					return;
				}
				if (usartGetChar(&c) == 0) return;

				// Same P1 logic as above
				if (c == 'f' || c == 'F') {
					g->p1.isActive = 0;
					printf("Player 1 has folded\n");
					g->turn = 2;
					printf("Player 2 Round River, Options: Fold, Call, Raise(F/C/R)\n");
					} 
				else if (c == 'c' || c == 'C') {
					printf("Player 1 has called\n");
					if (g->p2.currentBet > g->p1.currentBet) {
						uint16_t diff = g->p2.currentBet - g->p1.currentBet;
						if (diff >= g->p1.money) {
							g->p1.currentBet += g->p1.money;
							g->p1.money = 0;
							g->p1.allIn = 1;
							printf("Player 1 has gone all in!\n");
							} 
						else {
							g->p1.currentBet += diff;
							g->p1.money      -= diff;
							printf("Player 1 matched P2. Bet: %u, Money: %u\n",
							g->p1.currentBet, g->p1.money);
						}
					}
					g->turn = 2;
					printf("Player 2 Round River, Options: Fold, Call, Raise(F/C/R)\n");
					} 
				else if (c == 'r' || c == 'R') {
					printf("Player 1 has raised by 10\n");
					if (g->p1.money - 10 <= 0) {
						g->p1.currentBet += g->p1.money;
						g->p1.money = 0;
						g->p1.allIn = 1;
						printf("Player 1 has gone all in!\n");
						} 
					else {
						g->p1.money      -= 10;
						g->p1.currentBet += 10;
						printf("Player 1 raised by 10. Bet: %u, Money: %u\n",
						g->p1.currentBet, g->p1.money);
					}
					g->turn = 2;
					printf("Player 2 Round River, Options: Fold, Call, Raise(F/C/R)\n");
					} 
				else {
					printf("Invalid key\n");
				}
				g->pot = g->p1.currentBet + g->p2.currentBet;
			}
			else if (g->turn == 2) {
				if (g->p2.isActive == 0) {
					g->round = ROUND_SHOWDOWN;
					showdown = 1;
					g->turn  = 0;
					printf("Player 2 has already folded\n");
					return;
				}
				if (usartGetChar(&c) == 0) return;

				if (c == 'f' || c == 'F') {
					g->p2.isActive = 0;
					printf("Player 2 has folded\n");
					g->round = ROUND_SHOWDOWN;
					g->turn  = 0;
					showdown = 1;
					} 
				else if (c == 'c' || c == 'C') {
					printf("Player 2 has called\n");
					if (g->p1.currentBet > g->p2.currentBet) {
						uint16_t diff = g->p1.currentBet - g->p2.currentBet;
						if (diff >= g->p2.money) {
							g->p2.currentBet += g->p2.money;
							g->p2.money = 0;
							g->p2.allIn = 1;
							printf("Player 2 has gone all in!\n");
							} 
						else {
							g->p2.currentBet += diff;
							g->p2.money      -= diff;
							printf("Player 2 matched P1. Bet: %u, Money: %u\n",
							g->p2.currentBet, g->p2.money);
						}
					}
					g->round = ROUND_SHOWDOWN;
					showdown = 1;
					g->turn  = 0;
					} 
				else if (c == 'r' || c == 'R') {
					printf("Player 2 has raised by 10\n");
					if (g->p2.money - 10 <= 0) {
						g->p2.currentBet += g->p2.money;
						g->p2.money = 0;
						g->p2.allIn = 1;
						printf("Player 2 has gone all in!\n");
						} 
					else {
						g->p2.money      -= 10;
						g->p2.currentBet += 10;
						printf("Player 2 raised by 10. Bet: %u, Money: %u\n",
						g->p2.currentBet, g->p2.money);
					}
					g->turn = 1;
					printf("Player 1 Round River, Options: Fold, Call, Raise(F/C/R)\n");
					} 
				else {
					printf("Invalid key\n");
				}
				g->pot = g->p1.currentBet + g->p2.currentBet;
			}
				break;
			
		case ROUND_SHOWDOWN:
			if(showdown == 1){
				//Print Community Cards
				char c1[8],c2[8],c3[8],c4[8],c5[8];
				card_toString(g->community[0],c1,sizeof(c1));
				card_toString(g->community[1],c2,sizeof(c2));
				card_toString(g->community[2],c3,sizeof(c3));
				card_toString(g->community[3],c4,sizeof(c4));
				card_toString(g->community[4],c5,sizeof(c5));
				
				printf("\r\nCommunity Cards 1, 2, 3, 4, 5: %s %s %s %s %s\r\n", c1, c2, c3, c4, c5);
				//Print Player Cards
				char p1c1[8], p1c2[8];
				char p2c1[8], p2c2[8];

				card_toString(g->p1.card1, p1c1, sizeof(p1c1));
				card_toString(g->p1.card2, p1c2, sizeof(p1c2));
				card_toString(g->p2.card1, p2c1, sizeof(p2c1));
				card_toString(g->p2.card2, p2c2, sizeof(p2c2));

				printf("Player 1 cards: %s %s\r\n", p1c1, p1c2);
				printf("Player 2 cards: %s %s\r\n", p2c1, p2c2);
				printf("\r\n Showdown. Pot = %u\r\n", g->pot);
				
				//Calculate winner
				Card p1Cards[7];
				Card p2Cards[7];
				//Build 7 Card hands, 2 player and 5 community cards
				p1Cards[0] = g->p1.card1;
				p1Cards[1] = g->p1.card2;

				p2Cards[0] = g->p2.card1;
				p2Cards[1] = g->p2.card2;
				
				for(int i = 0; i < 5; i++){
					p1Cards[2+i] = g->community[i];
					p2Cards[2+i] = g->community[i];
				}
				
				HandValue h1 = evaluate_best_hand(p1Cards);
				HandValue h2 = evaluate_best_hand(p2Cards);
				
				int compare = compare_hands(&h1, &h2);
				if (compare > 0){
					printf("\r\n Player 1 wins with %s!\r\n", hand_type_to_string(h1.type));
					g->p1.money += g->pot;
				} 
				else if (compare < 0 ){
					printf("\r\n Player 2 wins with %s!\r\n", hand_type_to_string(h2.type));
					g->p1.money += g->pot;
				}
				else{
					printf("\r\nIt's a tie! Pot is split.\r\n");
					g->p1.money += g->pot / 2;
					g->p2.money += g->pot - (g->pot / 2);
				}
				
				printf("Player 1 money: %u\r\n", g->p1.money);
				printf("Player 2 money: %u\r\n", g->p2.money);
				g->pot = 0;
				printf("Press any key to return to menu\n");
			}
			showdown = 0;
			if(usartGetChar(&c) == 1){
				g->round = ROUND_MENU;
				g->turn = 0;
			}
			return;
			break;
	}
}


int main(void)
{
  
	clockInit();
	uart_init(3,9600,NULL);
	USART3.CTRLA |= USART_RXCIE_bm;   // enable RX Complete interrupt
	printf("\r\nUSART Enabled\r\n");
	timerInit();
	sei();
	
	_delay_ms(5);
	Game game;
	game_init(&game);
	

	/*
	deal_player_cards(&game);
	deal_player_cards(&game);

	char p1c1[8], p1c2[8];
	char p2c1[8], p2c2[8];

	card_toString(game.p1.card1, p1c1, sizeof(p1c1));
	card_toString(game.p1.card2, p1c2, sizeof(p1c2));
	card_toString(game.p2.card1, p2c1, sizeof(p2c1));
	card_toString(game.p2.card2, p2c2, sizeof(p2c2));

	printf("Player 1 cards: %s %s\r\n", p1c1, p1c2);
	printf("Player 2 cards: %s %s\r\n", p2c1, p2c2);
	*/
	
    while (1) 
    {
		game_step(&game);
    }
}

