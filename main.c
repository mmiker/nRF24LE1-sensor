#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "reg24le1.h"

#define LED			1	// LED enable
#define RF			1	// radio enable
#define DHT			1	// DHT21/22 enable
#define DS			1	// DS18B20 enable
#define ADC			1	// ADC based light sensor
#define SLEEP		1	// power save mode enable
#define AES_ENC		1	// use aes encryption
#define AES_DEC		0	// use aes decryption

#if (AES_ENC == 1 || AES_DEC == 1)
#define AES			1
#endif

#define DHTUSEP0	0	// P0.X
#define DHTUSEP1	1	// P1.X

#define LEDPIN	GPIO_PIN_ID_P0_1		// P0.1 - номер пина LED
#define DHTPIN	GPIO_PIN_ID_P1_4		// P1.4 - номер пина DHT21/22
#define DSPIN	GPIO_PIN_ID_P1_3		// P1.3 - номер пина DS18B20
#define LIGHTCH	ADC_CHANNEL_AIN0		// P0.0 - номер пина датчика освещенности
#define WDGPIN	GPIO_PIN_ID_P1_5		// P1.5 - номер пина sleep enable switch

typedef struct CONFIG CONFIG_T;
struct CONFIG{
	uint8_t sensorID;	// this sensor module ID
	uint8_t channel;	// radio channel: 0-199
	uint8_t datarate;	// data rate: 1-3
	uint8_t autoask;	// auto ask future
	uint8_t srvaddr[5];	// server address to send
	uint8_t maxsend;	// max message send retries
	uint16_t sleeptm;	// wakepup by watchdog timeout, s ~8.5min max (0x1-0x1FF LSB first)
#if AES
	uint8_t useaes;		// use aes encryption
	uint8_t aeskey[16];	// aes encryption key
#endif
};

__xdata __at(0xFC00) CONFIG_T config;

#define MSGLEN		16
// message format
typedef struct MESSAGE MESSAGE_T;
struct MESSAGE{
	uint8_t msgType;	// message type: 0 - info, 1 - sensor value, 2 - sensor error
	uint8_t sensorID;	// remote sensor ID
	uint8_t sensorType;	// sensor type: 0 - DS1820, 1 - BH1750, 2 - DHT сенсор, 3 - BMP085, 4 - ADC
	uint8_t valueType;	// value type: 0 - temperature, 1 - humidity, 2 - pressure, 3 - light, 4 - voltage
	uint8_t owkey[8];	// sensor id for 1-wire, sensor number for DHT in owkey[0]
	union	{			// sensor value depend of sensor type
		float	fValue;
		int32_t	iValue;
		uint8_t cValue[4]; // cValue[0] - DS18B20,DHT21/22 read error code
	} data;
};

//подключение необходимых функций:
#include "../SDK/src/delay/src/delay_us.c"
#include "../SDK/src/delay/src/delay_s.c"
#include "../SDK/src/delay/src/delay_ms.c"

#include "gpio.h"
#include "../SDK/src/gpio/src/gpio_pin_configure.c"
#include "../SDK/src/gpio/src/gpio_pin_val_clear.c"
#include "../SDK/src/gpio/src/gpio_pin_val_set.c"
#include "../SDK/src/gpio/src/gpio_pin_val_read.c"

#include "../SDK/src/interrupt/src/interrupt_configure_ifp.c"

#if ADC
#include "adc.h"
#include "../SDK/src/adc/src/adc_configure.c"
#include "../SDK/src/adc/src/adc_set_input_channel.c"
#include "../SDK/src/adc/src/adc_start_single_conversion.c"
#include "../SDK/src/adc/src/adc_start_single_conversion_get_value.c"
#endif

#if RF
#include "../SDK/src/rf/src/rf_read_rx_payload.c"
#include "../SDK/src/rf/src/rf_configure_debug_lite.c"
#include "../SDK/src/rf/src/rf_write_register.c"
#include "../SDK/src/rf/src/rf_spi_configure_enable.c"
#include "../SDK/src/rf/src/rf_write_tx_payload.c"
#include "../SDK/src/rf/src/rf_transmit.c"
#include "../SDK/src/rf/src/rf_set_as_rx.c"
#include "../SDK/src/rf/src/rf_irq_clear_all.c"
#include "../SDK/src/rf/src/rf_set_as_tx.c"
#include "../SDK/src/rf/src/rf_spi_execute_command.c"
#include "../SDK/src/rf/src/rf_spi_send_read.c"
#include "../SDK/src/rf/src/rf_power_up_param.c"
#include "../SDK/src/rf/src/rf_read_register.c"
#include "../SDK/src/rf/src/rf_spi_send_read_byte.c"
#include "../SDK/src/rf/src/rf_set_rx_addr.c"
#include "../SDK/src/rf/src/rf_power_down.c"
#include "../SDK/src/rf/src/rf_power_up.c"
#include "../SDK/src/rf/src/rf_tx_fifo_is_full.c"
#include "./nRFLE.c"
#endif

#if SLEEP
#include "../SDK/src/watchdog/src/watchdog_set_wdsv_count.c"
#include "../SDK/src/watchdog/src/watchdog_start_and_set_timeout_in_ms.c"
#include "../SDK/src/pwr_clk_mgmt/src/pwr_clk_mgmt_clklf_configure.c"
#endif

#if AES
#include "aes/include/aes.h"
#include "aes/include/aes_user_options.h"
#include "aes/include/enc_dec_accel.h"
#include "aes/src/enc_dec_accel_galois_multiply.c"
#include "aes/src/aes_get_sbox.c"
#include "aes/src/aes_add_round_key.c"
#include "aes/src/aes_sub_word.c"
#include "aes/src/aes_rot_word.c"
#include "aes/src/aes_get_rcon.c"
#include "aes/src/aes_key_expansion.c"
#include "aes/src/aes_initialize.c"
aes_data_t aes_data;
#endif


#if AES_ENC
#include "aes/src/aes_sub_bytes.c"
#include "aes/src/aes_mix_columns.c"
#include "aes/src/aes_shift_rows.c"
#include "aes/src/aes_encrypt_ecb.c"
#endif

#if AES_DEC
#include "aes/src/aes_get_sbox_inv.c"
#include "aes/src/aes_sub_bytes_inv.c"
#include "aes/src/aes_mix_columns_inv.c"
#include "aes/src/aes_shift_rows_inv.c"
#include "aes/src/aes_decrypt_ecb.c"
#endif

// DHT библиотеки
#if DHT
#define PIN0XVAL(p) ((P0 & (1 << (p % 8))) > 0 ? 1 : 0)		// read P0.X
#define PIN1XVAL(p) ((P1 & (1 << (p % 8))) > 0 ? 1 : 0)		// read P1.X

typedef enum {
	DHT_NO_ERROR,
	DHT_NO_RESPONSE,
	DHT_CHECKSUM_ERROR
} dhterror_t;

bool waitpin(uint8_t val) {
	uint8_t readtm = 80;
#if DHTUSEP0
	while ((PIN0XVAL(DHTPIN) != val) && (--readtm > 0));	// for P0.X only
#endif
#if DHTUSEP1
	while ((PIN1XVAL(DHTPIN) != val) && (--readtm > 0));	// for P1.X only
#endif
	if (readtm == 0) {
		return 0;
	}
	return 1;
}

/* temperature in 1/10 deg C, humidity in 1/10 % */
dhterror_t dhtread(int *temp, int *hum) {
	unsigned char j, i;
	unsigned char datadht[5] = {0,0,0,0,0};
	unsigned int crcdata = 0;

	//pin as output and set 0
	gpio_pin_configure(DHTPIN, GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT | GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_CLEAR);

	delay_ms(18);	// reset 1-20ms

	gpio_pin_val_set(DHTPIN);

	//pin as input
	gpio_pin_configure(DHTPIN, GPIO_PIN_CONFIG_OPTION_DIR_INPUT);

	//=============check DHT response
	if (!waitpin(0)) {
		return DHT_NO_RESPONSE;
	}
	if (!waitpin(1)) {
		return DHT_NO_RESPONSE;
	}
	//===============receive 40 data bits
	interrupt_control_global_disable();
	if (!waitpin(0)) {
		return DHT_NO_RESPONSE;
	}
	for (j=0; j<5; j++) {
		datadht[j]=0;
		for(i=0; i<8; i++) {
			if (!waitpin(1)) {
				return DHT_NO_RESPONSE;
			}
			delay_us(30);
			if (gpio_pin_val_read(DHTPIN))
				datadht[j]|=1<<(7-i);
			if (!waitpin(0)) {
				return DHT_NO_RESPONSE;
			}
        }
    }
	interrupt_control_global_enable();

    if (datadht[0]==0 && datadht[1]==0 && datadht[2]==0 && datadht[3]==0) {
    	//обработка ошибки:не подключен датчик !!
    	return DHT_NO_RESPONSE;
    }

	for(i = 0; i < 4; i++) crcdata += datadht[i];
    if ((crcdata & 0xff) != datadht[4]) {	// check CRC
    	/* обработка ошибки:ошибка CRC */
    	return DHT_CHECKSUM_ERROR;
    }

    if ((datadht[1]==0) && (datadht[3]==0)) {
    	// dht11
	    *hum=datadht[2]*10; // умножение на 10,чтобы было одинаково как у dht22,можно убрать.
	    *temp=datadht[0]*10;
    }
    else {
    	// dht22
    	*hum = ((unsigned int)datadht[0] << 8) | (unsigned int)datadht[1];
    	*temp = (((unsigned int)datadht[2] & 0x7F) << 8) | (unsigned int)datadht[3];
    	if (datadht[2] & 0x80) *temp *= -1;
	}
    //  тут имеем переменные hum и temp,которые можно отправить по радиоканалу

	return DHT_NO_ERROR;
}
#endif	//DHT

// OneWire библиотеки
#if DS
#define SKIP_ROM_CMD	0xcc
#define START_CONV_CMD	0x44
#define READ_SCR_CMD	0xbe
#define WRITE_SCR_CMD	0x4e
#define REG_TH			0x00
#define REG_TL			0xff
#define REG_CONFIG		0x3f	// precision 10bit

typedef enum {
	DS_NO_ERROR,
	DS_NOT_FOUND,
	DS_CRC_ERROR,
	DS_TIMEOUT
} dserror_t;

uint8_t OneWireReset(void) {
	uint8_t r = 1;
	gpio_pin_configure(DSPIN, GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT | GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_CLEAR);
	delay_us(480);
	gpio_pin_configure(DSPIN, GPIO_PIN_CONFIG_OPTION_DIR_INPUT);
	delay_us(60);
	r = !gpio_pin_val_read(DSPIN);
	delay_us(440);
	return r;
}

void OneWireOutByte(uint8_t d) {
	uint8_t n;
	interrupt_control_global_disable();

	for(n=8; n>0; n--) {
		if ((d & 0x01) == 1) {
		   gpio_pin_configure(DSPIN, GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT | GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_CLEAR);
		   delay_us(2); //5
		   gpio_pin_configure(DSPIN, GPIO_PIN_CONFIG_OPTION_DIR_INPUT);
		   delay_us(60);
		}
		else {
    	  gpio_pin_configure(DSPIN, GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT | GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_CLEAR);
    	  delay_us(60);
    	  gpio_pin_configure(DSPIN, GPIO_PIN_CONFIG_OPTION_DIR_INPUT);
		}
		d=d>>1;
	}
	interrupt_control_global_enable();
}

uint8_t OneWireInByte(void) {
	uint8_t d=0, n, b=0;
	interrupt_control_global_disable();

	for (n=0; n<8; n++) {
		gpio_pin_configure(DSPIN, GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT | GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_CLEAR);
		delay_us(2); // 5
		gpio_pin_configure(DSPIN, GPIO_PIN_CONFIG_OPTION_DIR_INPUT);
		delay_us(2); // 5
		b = gpio_pin_val_read(DSPIN);
		delay_us(50);
		d = (d >> 1) | (b<<7);
	}
	interrupt_control_global_enable();
	return(d);
}

dserror_t dsread(float *temp) {
    unsigned char SignBit;
    unsigned char data[2];
    int TReading;
    uint16_t convtm;

	gpio_pin_configure(DSPIN, GPIO_PIN_CONFIG_OPTION_DIR_INPUT);

	if (!OneWireReset()) {
		return DS_NOT_FOUND;
	}
	// 0xCC skip ROM
	OneWireOutByte(SKIP_ROM_CMD);

	// setting precision 10bit
	OneWireOutByte(WRITE_SCR_CMD);
	OneWireOutByte(REG_TH);
	OneWireOutByte(REG_TL);
	OneWireOutByte(REG_CONFIG);

	if (!OneWireReset()) {
		return DS_NOT_FOUND;
	}
	// 0xCC skip ROM
	OneWireOutByte(SKIP_ROM_CMD);
	// 0x44 start conversion
	OneWireOutByte(START_CONV_CMD);

	// wait while temperature value not ready
	convtm = 400;	// ~200ms
	while (OneWireInByte() != 0xff && --convtm > 0);
	// conversion timeout
	if (convtm == 0) {
		return DS_TIMEOUT;
	}

	if (!OneWireReset()) {
		return DS_NOT_FOUND;
	}
	OneWireOutByte(SKIP_ROM_CMD);
	// 0xbe get temperature from ram
	OneWireOutByte(READ_SCR_CMD);

	data[0] = OneWireInByte();
	data[1] = OneWireInByte();

	TReading = (int)(data[1] << 8) | (int)data[0];
	SignBit = TReading & 0x8000;
	if (SignBit) {
		TReading = (TReading ^ 0xffff) + 1;
	}

	gpio_pin_configure(DSPIN, GPIO_PIN_CONFIG_OPTION_DIR_INPUT);
	*temp = (float)((6 * TReading) + TReading / 4)/100;
	return DS_NO_ERROR;
}
#endif //DS

#if RF
void rfsend(const MESSAGE_T *msg) {
	uint16_t timeout; uint8_t retry = config.maxsend;
#if AES_ENC
	MESSAGE_T buf;
	if (config.useaes) {
		aes_encrypt_ecb(&aes_data, (unsigned char *) msg, (unsigned char *) buf);
	}
	else {
		memcpy(&buf, msg, MSGLEN);
	}
#endif
start:
	timeout = 300;	// 300*10us = 3ms
	if (rf_tx_fifo_is_full())
		rf_flush_tx();

	rf_irq_clear_all(); //clear all interrupts in the 24L01
#if AES_ENC
	rf_write_tx_payload((uint8_t*) buf, MSGLEN, true); //transmit received char over RF
#else
	rf_write_tx_payload((uint8_t*) msg, MSGLEN, true); //transmit received char over RF
#endif

	//wait until the packet has been sent or the maximum number of retries has been reached
	while(!(rf_irq_pin_active() && (rf_irq_tx_ds_active() || rf_irq_max_rt_active()))) {
		if (timeout-- == 0) { // checking timeout if nothing
			if (retry-- > 0)
				goto start;
			else
				break;
		}
		delay_us(10);	// 10us
	}
	if (rf_irq_pin_active() && rf_irq_max_rt_active()) { // checking max_rt bit
		if (retry-- > 0)
			goto start;
	}

	rf_irq_clear_all(); //clear all interrupts in the 24L01
}

uint8_t rfread(MESSAGE_T *msg, uint16_t timeout) {	// timeout in 10us intervals
	uint8_t state = 0;
#if AES_DEC
	MESSAGE_T buf;
#endif
	rf_set_as_rx(true); //change the device to an RX to get the character back from the other 24L01
	rf_irq_clear_all(); //clear interrupts again

	while (timeout-- > 0) {
		//wait a while to see if we get the data back (change the loop maximum and the lower if
		//  argument (should be loop maximum - 1) to lengthen or shorten this time frame
		if((rf_irq_pin_active() && rf_irq_rx_dr_active())) {
#if AES_DEC
			rf_read_rx_payload((uint8_t*) buf, MSGLEN); //get the payload into data
#else
			rf_read_rx_payload((uint8_t*) msg, MSGLEN); //get the payload into data
#endif
			state = 1;
			break;
		}
		delay_us(10);	// 10us
	}
#if AES_DEC
	if (config.useaes) {
		aes_decrypt_ecb(&aes_data, (unsigned char *) buf, (unsigned char *) msg);
	}
	else {
		memcpy(msg, &buf, MSGLEN);
	}
#endif
	rf_irq_clear_all(); //clear interrupts again
	rf_set_as_tx(); //resume normal operation as a TX
	return state;
}
#endif //RF

// main
void main(void) {

#if ADC
	int Light;
#endif

#if DHT
	int DHTTemp, DHTHum;
#endif

#if DS
    float DSTemp;
#endif

	MESSAGE_T message;
	message.sensorID = config.sensorID;

#if AES
	if (config.useaes) {
		aes_initialize(&aes_data, AES_KEY_LENGTH_128_BITS, config.aeskey, NULL);
	}
#endif

	delay_ms(20);

#if RF
 	radiobegin();					// init RF
	openAllPipe();					// setting TX/RX addr
	setAutoAck(config.autoask);
	setChannel(config.channel);		// radio channel setup
	setDataRate(config.datarate);	// 1 - 250кб , 2 - 1 мб , 3 -2 мб.
#endif

#if ADC
	adc_configure (ADC_CONFIG_OPTION_RESOLUTION_10_BITS|ADC_CONFIG_OPTION_REF_SELECT_VDD |ADC_CONFIG_OPTION_RESULT_JUSTIFICATION_RIGHT);
#endif

#if LED
	// настроим порт LED
	gpio_pin_configure(LEDPIN, 						// укажем необходимые параметры
			GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT |
			GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_CLEAR |
			GPIO_PIN_CONFIG_OPTION_PIN_MODE_OUTPUT_BUFFER_NORMAL_DRIVE_STRENGTH);
#endif
	
	while(1) {	// main loop

#if LED
		if (gpio_pin_val_read(LEDPIN)) {
			gpio_pin_val_clear(LEDPIN);		//установка 0
		}
		else {
			gpio_pin_val_set(LEDPIN);		//установка 1
		}
#endif

#if ADC
		adc_power_up();
		Light = adc_start_single_conversion_get_value(LIGHTCH);
		adc_power_down();
		message.msgType = 1;
		message.sensorType = 4;
		message.valueType = 3;
		message.data.iValue = Light;
		rfsend(&message);
#endif

#if DS
		message.msgType = 2;
		message.sensorType = 0;
		message.valueType = 0;
		message.owkey[0] = 0x0;
		if ((message.data.cValue[0] = dsread(&DSTemp)) == DS_NO_ERROR) {
			// DS18B20 temperature send
			message.msgType = 1;
			message.owkey[0] = 0x28;
			message.data.fValue = DSTemp;
		}
		rfsend(&message);
		gpio_pin_configure(DSPIN, GPIO_PIN_CONFIG_OPTION_DIR_INPUT);
#endif	//DS

#if DHT
		message.msgType = 2;
		message.sensorType = 2;
		message.owkey[0] = 1;
		if ((message.data.cValue[0] = dhtread(&DHTTemp, &DHTHum)) != DHT_NO_ERROR) {
			rfsend(&message);
		}
		else {
			// DHT temperature, humidity send
			message.msgType = 1;
			message.valueType = 0;
			message.data.iValue = DHTTemp;
			rfsend(&message);
			message.valueType = 1;
			message.data.iValue = DHTHum;
			rfsend(&message);
		}
		gpio_pin_configure(DHTPIN, GPIO_PIN_CONFIG_OPTION_DIR_INPUT);
#endif	//DHT

#if SLEEP
		gpio_pin_configure(WDGPIN, GPIO_PIN_CONFIG_OPTION_DIR_INPUT);
   		if (gpio_pin_val_read(WDGPIN)) {	// WDGPIN = 1

   			CLKLFCTRL = 1;
			gpio_pin_val_clear(LEDPIN);		//установка 0

			OPMCON = 2;
			watchdog_start_and_set_timeout_in_ms((uint32_t)config.sleeptm * 1000);
   			//pwr_clk_mgmt_enter_pwr_mode_memory_ret_tmr_on(); // 1mkA
   			pwr_clk_mgmt_enter_pwr_mode_register_ret(); // 450 mkA
   		}
#endif
   		delay_ms(2000); //пауза
	} // main loop
}