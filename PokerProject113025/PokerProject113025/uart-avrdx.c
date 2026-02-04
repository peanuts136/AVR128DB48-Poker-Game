/*
 * AVRDx specific UART code
 */

/* CPU frequency */
#define F_CPU 16000000UL


#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <avr/io.h>
#include <avr/interrupt.h>

#include "uart.h"

void* usart_init(uint8_t usartnum, uint32_t baud_rate)
{
    USART_t* usart;

    if (usartnum == 0) {
        usart = &USART0;
        // enable USART0 TX pin
        PORTA.DIRSET = PIN0_bm;
		PORTA.DIRCLR = PIN1_bm;
    }
    else if (usartnum == 1) {
        usart = &USART1;
        // enable USART1 TX pin
        PORTC.DIRSET = PIN0_bm;
		PORTC.DIRCLR = PIN1_bm;
    }
    else if (usartnum == 2) {
        usart = &USART2;
        // enable USART2 TX pin
        PORTF.DIRSET = PIN0_bm;
		PORTF.DIRCLR = PIN1_bm;
    }
#ifdef USART3
    else if (usartnum == 3) {
        usart = &USART3;
        // enable USART3 TX pin
        PORTB.DIRSET = PIN0_bm;
		PORTB.DIRCLR = PIN1_bm;
    } 
#endif
#ifdef USART4
    else if (usartnum == 4) {
        usart = &USART4;
        // enable USART4 TX pin
        PORTE.DIRSET = PIN0_bm;
		PORTE.DIRCLR = PIN1_bm;
    }
#endif
#ifdef USART5
    else if (usartnum == 5) {
        //usart = &USART5;
        // enable USART5 TX pin
        //PORTG.DIRSET = PIN0_bm;
		//PORTB.DIRCLR = PIN1_bm;
    }
#endif
    else {
        usart = NULL;
    }

    usart->BAUD = (4UL * F_CPU) / baud_rate;
    usart->CTRLB |= (USART_RXEN_bm | USART_TXEN_bm); /* tx/rx enable */

    return usart;
}

void usart_transmit_data(void* ptr, char c)
{
    USART_t* usart = (USART_t*)ptr;
    usart->TXDATAL = c;
}

void usart_wait_until_transmit_ready(void *ptr)
{
    USART_t* usart = (USART_t*)ptr;
    loop_until_bit_is_set(usart->STATUS, USART_DREIF_bp);
}

int usart_receive_data(void* ptr)
{
    USART_t* usart = (USART_t*)ptr;

    uint8_t c;

    loop_until_bit_is_set(usart->STATUS, USART_RXCIF_bp);
    char rcv_status = usart->RXDATAH;
    if (rcv_status & USART_FERR_bm) {
        c = usart->RXDATAL; /* clear error by reading data */
        return _FDEV_EOF;
    }
    if (rcv_status & USART_BUFOVF_bm) {
        c = usart->RXDATAL; /* clear error by reading data */
        return _FDEV_ERR;
    }
    c = usart->RXDATAL;
    return c;
}
