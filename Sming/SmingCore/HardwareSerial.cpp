/****
 * Sming Framework Project - Open Source framework for high efficiency native ESP8266 development.
 * Created 2015 by Skurydin Alexey
 * http://github.com/anakod/Sming
 * All files of the Sming Core are provided under the LGPL v3 license.
 ****/

// HardwareSerial based on Espressif Systems code

#include "../SmingCore/HardwareSerial.h"
#include "../Wiring/WiringFrameworkIncludes.h"
#include <cstdarg>

#include "../SmingCore/Clock.h"
#include "../SmingCore/Interrupts.h"

// UartDev is defined and initialized in ROM code.
extern UartDevice UartDev;

HardwareSerial::HardwareSerial(const int uartPort)
	: uart(uartPort)
{
}

void HardwareSerial::begin(const uint32_t baud/* = 9600*/)
{
	//TODO: Move to params!
	UartDev.baut_rate = (UartBautRate)baud;
	UartDev.parity = NONE_BITS;
	UartDev.exist_parity = STICK_PARITY_DIS;
	UartDev.stop_bits = ONE_STOP_BIT;
	UartDev.data_bits = EIGHT_BITS;

	ETS_UART_INTR_ATTACH((void*)uart0_rx_intr_handler,  &(UartDev.rcv_buff));
	PIN_PULLUP_DIS(PERIPHS_IO_MUX_U0TXD_U);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD);

	uart_div_modify(uart, UART_CLK_FREQ / (UartDev.baut_rate));

	WRITE_PERI_REG(UART_CONF0(uart),    UartDev.exist_parity
				   | UartDev.parity
				   | (UartDev.stop_bits << UART_STOP_BIT_NUM_S)
				   | (UartDev.data_bits << UART_BIT_NUM_S));


	//clear rx and tx fifo,not ready
	SET_PERI_REG_MASK(UART_CONF0(uart), UART_RXFIFO_RST | UART_TXFIFO_RST);
	CLEAR_PERI_REG_MASK(UART_CONF0(uart), UART_RXFIFO_RST | UART_TXFIFO_RST);

	//set rx fifo trigger
	WRITE_PERI_REG(UART_CONF1(uart), (UartDev.rcv_buff.TrigLvl & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S);

	//clear all interrupt
	WRITE_PERI_REG(UART_INT_CLR(uart), 0xffff);
	//enable rx_interrupt
	SET_PERI_REG_MASK(UART_INT_ENA(uart), UART_RXFIFO_FULL_INT_ENA);

	ETS_UART_INTR_ENABLE();
	delay(10);
	Serial.println("\r\n"); // after SPAM :)
}

size_t HardwareSerial::write(uint8_t oneChar)
{
	//if (oneChar == '\0') return 0;

	uart_tx_one_char(oneChar);

	return 1;
}

int HardwareSerial::available()
{
	RcvMsgBuff &rxBuf = UartDev.rcv_buff;
	if (rxBuf.pWritePos != rxBuf.pReadPos)
	{
		if (rxBuf.pWritePos > rxBuf.pReadPos)
			return rxBuf.pWritePos - rxBuf.pReadPos;
		else
			return (((uint32_t)rxBuf.pRcvMsgBuff + RX_BUFF_SIZE) - (uint32_t)UartDev.rcv_buff.pReadPos) + (uint32_t)rxBuf.pWritePos;
	}
	else
		return 0;
}

int HardwareSerial::read()
{
	if (available() == 0)
		return -1;

	noInterrupts();
	RcvMsgBuff &rxBuf = UartDev.rcv_buff;
	char res = *(rxBuf.pReadPos);
	rxBuf.pReadPos++;

	if (rxBuf.pReadPos == (rxBuf.pRcvMsgBuff + RX_BUFF_SIZE))
		rxBuf.pReadPos = rxBuf.pRcvMsgBuff ;

	interrupts();
	return res;
}


int HardwareSerial::peek()
{
	if (available() == 0)
		return -1;

	noInterrupts();
	RcvMsgBuff &rxBuf = UartDev.rcv_buff;
	char res = *(rxBuf.pReadPos);
	interrupts();
	return res;
}

void HardwareSerial::flush()
{
}


/*void HardwareSerial::printf(const char *fmt, ...)
{
	// Doesn't work :(
	va_list va;
	va_start(va, fmt);
	ets_uart_printf(fmt, va);
	va_end(va);
}*/

void HardwareSerial::systemDebugOutput(bool enabled)
{
	if (uart == UART_ID_0)
		os_install_putc1(enabled ? (void *)uart_tx_one_char : NULL);
	//else
	//	os_install_putc1(enabled ? (void *)uart1_tx_one_char : NULL); //TODO: Debug serial
}

void HardwareSerial::uart0_rx_intr_handler(void *para)
{
    /* uart0 and uart1 intr combine togther, when interrupt occur, see reg 0x3ff20020, bit2, bit0 represents
     * uart1 and uart0 respectively
     */
    RcvMsgBuff *pRxBuff = (RcvMsgBuff *)para;
    uint8 RcvChar;

    if (UART_RXFIFO_FULL_INT_ST != (READ_PERI_REG(UART_INT_ST(UART_ID_0)) & UART_RXFIFO_FULL_INT_ST))
        return;

    WRITE_PERI_REG(UART_INT_CLR(UART_ID_0), UART_RXFIFO_FULL_INT_CLR);

    while (READ_PERI_REG(UART_STATUS(UART_ID_0)) & (UART_RXFIFO_CNT << UART_RXFIFO_CNT_S))
    {
        RcvChar = READ_PERI_REG(UART_FIFO(UART_ID_0)) & 0xFF;

        /* you can add your handle code below.*/

        *(pRxBuff->pWritePos) = RcvChar;

        // insert here for get one command line from uart
        if (RcvChar == '\n' )
            pRxBuff->BuffState = WRITE_OVER;

        pRxBuff->pWritePos++;

        if (pRxBuff->pWritePos == (pRxBuff->pRcvMsgBuff + RX_BUFF_SIZE))
        {
            // overflow ...we may need more error handle here.
        	pRxBuff->pWritePos = pRxBuff->pRcvMsgBuff;
        }

        if (pRxBuff->pWritePos == pRxBuff->pReadPos)
        {   // Prevent readbuffer overflow
			if (pRxBuff->pReadPos == (pRxBuff->pRcvMsgBuff + RX_BUFF_SIZE))
			{
				pRxBuff->pReadPos = pRxBuff->pRcvMsgBuff ;
			} else {
				pRxBuff->pReadPos++;
			}
		}
    }
}


HardwareSerial Serial(UART_ID_0);
