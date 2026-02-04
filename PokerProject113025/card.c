/*
 * card.c
 *
 * Created: 11/29/2025 12:39:04 AM
 *  Author: heton
 */ 

#include "card.h"
#include <stdlib.h>
#include <stdio.h>
#include "uart.h"

static Card deck[52];
static uint8_t deckIndex = 0;

void card_init(){
	uint8_t k = 0;
	
	for (uint8_t s = 0; s < 4; s++){
		for(uint8_t r=1; r<=13; r++){
			deck[k].rank= r;
			deck[k].suit = s;
			k++;
		}
	}
}

void card_shuffle(uint16_t seed){
	srand(seed); //This will be from the ticks or preset number
	deckIndex = 0; //Ensures we start at top
	//Utilize the fisher-yates shuffle
	for(int i=51; i > 0; i--){
		//Pick random index from 0 to i
		uint8_t j = rand() % (i+1);
		
		//Swap deck[i] and deck[j]
		Card temp = deck[i];
		deck[i] = deck[j];
		deck[j] = temp;
	}
}

Card get_card(uint8_t index){
	return deck[index];
}
void player_init(Player *p, uint16_t startingMoney){
	p->money = startingMoney;
	p->currentBet = 0;
	p->isActive = 1;
}
void card_toString(Card c, char *buf, uint8_t bufSize)
{
	const char* suits[] = {"H", "D", "C", "S"};
	const char* ranks[] = {"?", "A","2","3","4","5","6","7","8","9","10","J","Q","K"};

	snprintf(buf, bufSize, "%s%s", ranks[c.rank], suits[c.suit]);
}


Card draw_card(){
	if (deckIndex >= 52){
		deckIndex = 0;
		card_shuffle(12345);
	}
	return deck[deckIndex++]; //Get the topmost card and move onto the next 1
}

void deal_player_cards(Game *g){
	g->p1.card1 = draw_card();
	g->p1.card2 = draw_card();
	
	g->p2.card1 = draw_card();
	g->p2.card2 = draw_card();
}

void game_init(Game *g){
	g->round = ROUND_MENU;
	g->turn = 0;
	g->pot = 0;
	
	player_init(&g->p1, 1000);
	player_init(&g->p2, 1000);
	
	g->communityCount = 0;
	card_init();
	card_shuffle(12345);
}



void deal_community(Game *g, uint8_t count){
	for(int i = 0; i < count; i++){
		g->community[g->communityCount++] = draw_card();
	}
}

static uint8_t rank_value(uint8_t rank){
	//A should be 14, others can be normal e.g 2 = 2, J=11
	if (rank == 1){
		return 14;
	}
	return rank;
}

static uint8_t find_high_card_of_straight(const uint8_t rankPresent[15]){
	//returns high card of straight (5-14) or 0 if no straight exists
	//rankPresent is if that rank exists within the 5 card combination
	
	for(int high = 14; high >= 5; high--){
		uint8_t ok = 1;
		for(int k = 0; k < 5; k++){
			if(rankPresent[high-k] == 0){
				ok = 0;
				break;
			}
		}
		if(ok == 1){
			return high; 
		}
	}
	
	//Special case, A-2-3-4-5, treat high as 5
	if(rankPresent[14] && rankPresent[2] && rankPresent[3] && rankPresent[4] && rankPresent[5]){
		return 5;
	}
	return 0;
}

HandValue evaluate_best_hand(Card cards[7]){
	
	HandValue hv;
	hv.type = HAND_HIGH_CARD; //Default
	
	uint8_t rankCount[15] = {0}; //Count how many times rank appears 
	uint8_t suitCount[4] = {0}; //0,1,2,3 = suits, count how many times suits appears
	uint8_t rankPresent[15] = {0}; //1 if at least one card of a certain rank exists
	
	//Getting the counts
	for(int i = 0; i < 7; i++){
		uint8_t rank = rank_value(cards[i].rank);
		rankCount[rank]++;
		rankPresent[rank] = 1;
		
		suitCount[cards[i].suit]++;
	}
	
	//Check for possible flushes
	int flushSuit = -1; //If not -1, then a flush is possible
	for(int s = 0; s < 4; s++){
		//0 = Hearts, 1 = Diamonds, 2= Clubs, 3 = Spade
		if(suitCount[s] >= 5){
			flushSuit = s;
			break;
		}
	}
	//Checking for rank patterns: 4 and 3 of a kind, pairs
	
	uint8_t fourRank = 0; //Ranks that appears 4 times, or 0 if none
	uint8_t threeRank[3]; uint8_t threeCount = 0; //All ranks with 3 copies
	uint8_t pairRank[4]; uint8_t pairCount = 0; //All ranks with 2 copies
	
	for(int v = 14; v>=2; v--){
		if(rankCount[v] == 4){
			fourRank = (uint8_t)v;
		}
		
		else if(rankCount[v] == 3){//Get the highest threeCount rank at the first index, second highest at 2nd index
			if(threeCount < 3){
				threeRank[threeCount++] = (uint8_t)v;
			}
		}
		else if(rankCount[v] == 2){//Get the highest pairCount rank at the first index, second highest at 2nd, 3rd at 3rd
			if (pairCount < 4){
				pairRank[pairCount++] = (uint8_t)v;
			}
		}
	}
	
	//Straight Flush/Royal Flush
	if (flushSuit != -1){//There is at least 5 cards with same suit
		uint8_t flushRankPresent[15] = {0};
		
		//Get the ranks of that suit only
		for(int i = 0; i < 7; i++){
			if(cards[i].suit == flushSuit){
				uint8_t v = rank_value(cards[i].rank);
				flushRankPresent[v] = 1;
				
			}
		}
		uint8_t straightFlushHigh = find_high_card_of_straight(flushRankPresent);
		if(straightFlushHigh != 0){
			if(straightFlushHigh == 14){
				hv.type = HAND_ROYAL_FLUSH;
			}
			else{
				hv.type = HAND_STRAIGHT_FLUSH;
			}
			hv.ranks[0] = straightFlushHigh;
			return hv;
		}
	}
	
	//Four of a kind
	if (fourRank != 0){
		hv.type = HAND_FOUR_OF_A_KIND;
		hv.ranks[0] = fourRank;
		//Kicker/highest other card
		for(int v = 14; v >= 2; v--){
			if(v == fourRank)continue;
			if(rankCount[v] > 0){//Second index is the kicker
				hv.ranks[1] = (uint8_t)v;
				break;
			}
		}
		return hv; 
	}
	//Full house
	if((threeCount >= 1 && pairCount >= 1) || (threeCount >= 2)){ //One triple and one pair or 2 triples count as full house
		hv.type = HAND_FULL_HOUSE;
		uint8_t triple = threeRank[0];
		uint8_t pair = 0;
		
		if(threeCount >= 2){
			pair = threeRank[1]; //Use the second triple as the "pair"
		}
		else{
			pair = pairRank[0];
		}
		hv.ranks[0] = triple;
		hv.ranks[1] = pair;
		return hv;
	}
	//Flush no straight
	if(flushSuit != -1){
		hv.type = HAND_FLUSH;
		uint8_t flushRanks[7];	//Collection of 7 possible cards in the flushsuit
		uint8_t flushCount = 0; //Number of cards that matches the flushsuit
		
		//Collect all cards in flush suit in descending order
		for(int v = 14; v>=2; v--){
			for(int i = 0; i < 7; i++){
				if(cards[i].suit == flushSuit && rank_value(cards[i].rank) == v){
					flushRanks[flushCount++] = (uint8_t)v;
					break;
				}
			}
		}
		//Keep top 5
		for(int i = 0; i < 5 && i < flushCount; i++){
			hv.ranks[i] = flushRanks[i];
		}
		return hv;
	}
	
	//Straight no flush
	uint8_t straightHigh = find_high_card_of_straight(rankPresent); //Find straight if exists and returns the highest rank
	if(straightHigh != 0){ //If highest rank is not 0, then straight exists
		hv.type = HAND_STRAIGHT;
		hv.ranks[0] = straightHigh;
		return hv;
	}
	
	//Three of a kind
	if(threeCount >= 1){
		hv.type = HAND_THREE_OF_A_KIND;
		hv.ranks[0] = threeRank[0];
		
		//Get the kicker cards
		int index = 1;
		for(int v = 14; v>=2 && index < 3; v--){
			if(v == threeRank[0]) continue;
			if(rankCount[v] > 0){
				hv.ranks[index++] = (uint8_t)v;
			}
		}
		return hv;
	}
	
	//Two Pair
	if(pairCount >= 2 ){
		hv.type = HAND_TWO_PAIR;
		hv.ranks[0] = pairRank[0]; //Highest pair
		hv.ranks[1] = pairRank[1]; //second pair
		
		//Kicker
		for(int v = 14; v>=2; v++){
			if(v == pairRank[0] || v == pairRank[1]) continue;
			if(rankCount[v] > 0){
				hv.ranks[2] = (uint8_t)v;
				break;
			}
		}
		return hv;
	}
	
	//One pair
	if(pairCount == 1){
		hv.type = HAND_ONE_PAIR;
		hv.ranks[0] = pairRank[0];
		
		//Kickers
		int index = 1;
		for(int v = 14; v >= 2 && index < 4; v--){
			if(v == pairRank[0]) continue;
			if(rankCount[v] > 0){
				hv.ranks[index++] = (uint8_t)v;
			}
		}
		return hv;
	}
	
	//High Card
	hv.type = HAND_HIGH_CARD;
	int index = 0;
	for(int v = 14; v>=2 && index < 5; v--){
		if(rankCount[v]>0){
			hv.ranks[index++] = (uint8_t)v;
		}
	}
	return hv;
	
}

int compare_hands(const HandValue *a , const HandValue *b){
	if (a->type > b->type) return 1;
	if (a->type < b->type) return -1;
	
	//If same type, compare ranks for high card
	for (int i = 0; i < 5; i++){
		if(a->ranks[i] > b->ranks[i]) return 1;
		if(a->ranks[i] < b->ranks[i]) return -1;
	}
	
	return 0; //Tie
}

const char* hand_type_to_string(HandRankType t)
{
	switch (t) {
		case HAND_HIGH_CARD:        return "High Card";
		case HAND_ONE_PAIR:         return "One Pair";
		case HAND_TWO_PAIR:         return "Two Pair";
		case HAND_THREE_OF_A_KIND:  return "Three of a Kind";
		case HAND_STRAIGHT:         return "Straight";
		case HAND_FLUSH:            return "Flush";
		case HAND_FULL_HOUSE:       return "Full House";
		case HAND_FOUR_OF_A_KIND:   return "Four of a Kind";
		case HAND_STRAIGHT_FLUSH:   return "Straight Flush";
		case HAND_ROYAL_FLUSH:      return "Royal Flush";
		default:                    return "Unknown";
	}
}