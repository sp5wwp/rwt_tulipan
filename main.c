#include <avr/io.h>
#include <inttypes.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <string.h>

//ustawienia usartu
#define F_CPU			14745600
#define USART0_BAUDRATE 19200   
#define USART0_PRESCALE F_CPU/16/USART0_BAUDRATE-1
#define MAX_USART0_LEN	16
//ustawienia inne
#define	MAX_NUM_LEN		16

volatile char 		msg[MAX_USART0_LEN];			//
volatile char		msg_index=0;					//zmienna dla indeksowania elementow tablicy msg[]
volatile char 		dial[MAX_NUM_LEN];				//
volatile uint8_t 	index=0;						//zmienna dla indeksowania elementow tablic
volatile char 		aux[MAX_NUM_LEN+6];				//pomocniczy string
volatile char 		rcv=0;							//
volatile uint8_t 	num=0;							//licznik 'klikniec' TN

//---------------------------------USART----------------------------------------
//initialize USART0 uC hardware
void USART0_init(void)
{
	//using 8-N-1
	//set baud rate
	UBRRL = USART0_PRESCALE;
	UBRRH = (USART0_PRESCALE >> 8);  

	//Enable receiver and transmitter
	UCSRB = ((1<<TXEN)|(1<<RXEN));
}

//read 1 byte via USART0
uint8_t USART0_getByte(void)
{
	//wait for receive complete flag
	while(!(UCSRA & (1<<RXC)));
	//go get it!
	return UDR;
}

//send 1 byte via USART0
void USART0_sendByte(uint8_t data)
{
  //wait until last byte has been transmitted
  while(!(UCSRA &(1<<UDRE)));

  //transmit data
  UDR = data;
}

//send a packet of bytes via USART0
void USART0_sendString(uint8_t string[MAX_USART0_LEN])
{
	UCSRB&=~(1<<RXEN);

	for(uint8_t i=0; i<=strlen(string)-1; i++)
	{
		USART0_sendByte(string[i]);
	}
	
	USART0_sendByte(0x0D); //13
	USART0_sendByte(0x0A); //10 (CRLF ending)
	
	UCSRB|=(1<<RXEN);
}

//enable USART0 byte-received interrupt
void USART0_int_enable(void)
{
	UCSRB |= (1<<RXCIE);
}

//disable USART0 byte-received interrupt
void USART0_int_disable(void)
{
	UCSRB &= ~(1<<RXCIE);
}

//INTs config
void INT_init(void)
{
	MCUCR |= (1<<ISC10)|(1<<ISC01); 	//falling edge INT0, logic change INT1
	GICR |= (1<<INT0)|(1<<INT1);		//activate
}

//timer config
void TIM_init(void)
{
	TCCR1B&=~((1<<CS10)|(1<<CS11)|(1<<CS12));
	TIMSK|=(1<<TOIE1); //wl. przerwanie
	TCNT1L=0; TCNT1H=0; //wyzerowanie
	
	TCCR0|=(1<<CS02)|(1<<CS01)|(1<<CS00); //narastajace zbocze - podniesienie mikrotelefonu
	TIMSK|=(1<<TOIE0); //wl. przerwanie
	TCNT0=0xFF;
}

//czyszczenie buforow
void buf_clr(void)
{
	for(uint8_t i=0; i<MAX_NUM_LEN-1; i++)
		dial[i]=0;
	for(uint8_t i=0; i<MAX_NUM_LEN+6-1; i++)
		aux[i]=0;
}

//dzwonek
void ring_in(void)
{
	if(!(PIND&(1<<4))) //opuszczona sluchawka
	{
		PORTC|=(1<<5); //zatyrkaj
		_delay_ms(64);
		PORTC&=~(1<<5);
	}
}

int main(void)
{
	PORTB=0xFF;			DDRB=0xFF;
	PORTC=0x00;			DDRC=0xFF;
	PORTD=0b00011100;	DDRD=0b00000010;

	_delay_ms(1000);
	buf_clr();
	USART0_init();
	USART0_int_enable();
	INT_init();
	TIM_init();
	//GSM init
	PORTC|=(1<<4);
	_delay_ms(1500);
	PORTC&=~(1<<4);
	
	sei();
	
	while(1);
	
	return 0;
}

ISR(USART_RXC_vect)	//odbior komend AT
{
	rcv=UDR;
	
	if(rcv!=10)
	{
		msg[msg_index]=rcv;
		msg_index++;
	}
	else
	{
		msg_index=0;
		if(msg[0]=='R' && msg[1]=='I' && msg[2]=='N' && msg[3]=='G')
			ring_in();
	}

	//jesli komenda "RING" - dzwon
	//jesli "NO CARRIER" - polaczenie nieodebrane... ;-)
}

ISR(INT0_vect)
{
	num++;
}

ISR(INT1_vect)
{
	_delay_ms(50);
	
	if(PIND&(1<<3)) //po kliknieciach TN
	{
		dial[index]=(48+(num%10)); //ascii 48+num%10
		index++;
		TCNT1L=0; TCNT1H=0;
		TCCR1B|=(1<<CS10)|(1<<CS12);
	}
	else //przed kliknieciami TN
	{
		num=0;
	}
}

ISR(TIMER1_OVF_vect)
{
	UCSRB &=~(1<<RXEN);
	TCCR1B&=~((1<<CS10)|(1<<CS12));
	
	index=0;
	
	if(PIND&(1<<4))//dzwonimy tylko, gdy jest podniesiona sluchawka
	{
		sprintf(aux, "ATD00%s;", dial);
		USART0_sendString(aux);
		buf_clr();
	}
	
	TCNT1L=0; TCNT1H=0;
}

//rozroznianie odlozenia od podniesienia sluchawki
ISR(TIMER0_OVF_vect)
{
	_delay_ms(50);
	
	if(PIND&(1<<4))	//podniesienie sluchawki
	{
		//UCSRB &=~(1<<RXEN);
		USART0_sendString("ATA");
		buf_clr();
		index=0;
		TCCR0&=~(1<<CS00);
	}
	else	//odlozenie sluchawki
	{
		//UCSRB &=~(1<<RXEN);
		USART0_sendString("ATH");
		buf_clr();
		index=0;
		TCCR0|=(1<<CS00);
	}
	
	TCNT0=0xFF;
}
