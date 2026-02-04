/*
 * card.h
 *
 * Created: 11/29/2025 12:35:38 AM
 *  Author: heton
 */ 

#ifndef CARD_H
#define CARD_H

#include <stdint.h>

typedef enum{
	SUIT_HEARTS = 0,
	SUIT_DIAMONDS = 1,
	SUIT_CLUBS = 2,
	SUIT_SPADE = 3
} Suit;

typedef struct {
	uint8_t rank; //Can be 1-13
	Suit suit; //Can be SUIT_HEARTS,...etc 
} Card;

typedef struct {
	Card card1;
	Card card2;
	uint16_t money; //Total money player has
	uint16_t currentBet; //how much money they have placed in the pot this round
	uint8_t isActive; //1 = still in, 0 = folded
	uint8_t allIn; //Has gone all in, unable to bet more, still contesting
} Player;

typedef enum{
	ROUND_MENU = 0, //Ask to play 2 person Poker
	ROUND_FLOP = 1, //Blinds, deal 2 cards to each player, deal flop or 3 person community
	ROUND_TURN = 2, //Deal 4th community card
	ROUND_RIVER = 3, //Deal 5th community card
	ROUND_SHOWDOWN = 4 //Showdown
} RoundState;

typedef enum{
	ACT_USART = 0, //Usart Prompts and transitions
	ACT_PLAYER1 = 1, //Player 1 fold, raise, call action
	ACT_PLAYER2= 2 //Player 2 fold, raise, call action
} TurnState;

typedef struct{
	RoundState round;
	TurnState turn;
	uint16_t pot;
	
	Player p1;
	Player p2;
	
	Card community[5];
	uint8_t communityCount;	
} Game;

typedef enum{
	HAND_HIGH_CARD = 0,
	HAND_ONE_PAIR = 1,
	HAND_TWO_PAIR = 2,
	HAND_THREE_OF_A_KIND = 3,
	HAND_STRAIGHT = 4,
	HAND_FLUSH = 5,
	HAND_FULL_HOUSE = 6,
	HAND_FOUR_OF_A_KIND = 7,
	HAND_STRAIGHT_FLUSH = 8,
	HAND_ROYAL_FLUSH = 9
} HandRankType;

typedef struct{
	HandRankType type;
	uint8_t ranks[5]; //For tie breakers
} HandValue;
void card_init();
void card_shuffle(uint16_t seed);
Card get_card(uint8_t index);
void card_toString(Card c, char *buf, uint8_t bufSize);

void player_init(Player *p, uint16_t startingMoney);
Card draw_card();
void deal_player_cards(Game *g);
void game_init(Game *g);
void deal_community(Game *g, uint8_t count);

HandValue evaluate_best_hand(Card cards[7]); //Evaluate best 5 card hand from 7 cards

int compare_hands(const HandValue *a, const HandValue *b); //Compare two hands, returns > 0 if a>b, <0 if a<b, 0 if tie

const char* hand_type_to_string(HandRankType t); //Get string name for hand type
#endif