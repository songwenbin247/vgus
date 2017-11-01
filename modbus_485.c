#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>
#include <unistd.h>
#include <time.h>
#include "usr-410s.h"
#include "modbus_485.h"
#include "vgus.h"
#define pr() printf("%s %s %d\n", __FILE__, __func__, __LINE__)

static unsigned short  ModBusCRC (unsigned char *ptr, int size) 
{
	unsigned short a,b,tmp,CRC16,V;    
	CRC16=0xffff;
	for (a=0;a<size;a++){
		CRC16=*ptr^CRC16;
			for (b=0;b<8;b++){
				tmp=CRC16 & 0x0001;
				CRC16 =CRC16 >>1;
				if (tmp)
					CRC16=CRC16 ^ 0xa001;
			}  
			*ptr++; 
	}  
	V = ((CRC16 & 0x00FF) << 8) | ((CRC16 & 0xFF00) >> 8); 
	return V;   
}

#define HEATING_SCORE_MAX 10
#define COOLING_SCORE_MAX 10

struct modbus_info
{
	unsigned int temperature;
	unsigned int warn_max;
	unsigned int warn_min;
	unsigned int heating_score;
	unsigned int cooling_score;
	unsigned int extremity;

};

struct modbus_info modbus_info = {240, 75, 25, 0, 0, 80};

static int get_temperature(struct send_info *info_485)
{
	unsigned char rbuf[32];
	char sbuf[] = {0x01, 0x03, 0x00, 0x00,0x00, 0x02, 0x3d, 0xb9};
	unsigned int tem;
	int len;
	unsigned short crc;
pr();
	copy_to_buf(info_485, sbuf, sizeof(sbuf));
pr();
	len = send_and_recv_data(info_485, (char *)rbuf, 32);
       	if (len < 2){
		return -1;
	}

pr();
	 crc = ModBusCRC(rbuf, len - 2);
	 if (crc != ((rbuf[len - 2] << 8) + rbuf[len - 1])){
	 	return -1;
	 }
	
pr();
	 tem = (rbuf[3] << 8) + rbuf[4];	
	if (tem == 0)
		tem = (rbuf[5] << 8) + rbuf[6];	

	return tem;
	
}

int cycle_period = 5;
int cycle_value = 0; 
#define HEATER_SW   0x01
#define COOLER_SW   0x00
#define open_heater(sw)  (sw) |= (1 << HEATER_SW)
#define open_cooler(sw)  (sw) |= (1 << COOLER_SW)
static char get_relay_value()
{
	char sw = 0;
	if (modbus_info.heating_score - cycle_value > 0)
		open_heater(sw);
	if (modbus_info.cooling_score - cycle_value > 0)
		open_cooler(sw);
	cycle_value++;
	if (cycle_value <  HEATING_SCORE_MAX)
		cycle_value = 0;
	return sw;
}

char relay_buf[] = {0x02, 0x0F, 0x00, 0x00, 0x00, 0x08, 0x01, 0x00};
int set_relay(struct send_info *info_485)
{
	unsigned char rbuf[32];
	int len;
	unsigned short crc;
	char sw = get_relay_value();
	if (sw == relay_buf[7])
		return 0;
	else
		relay_buf[7] = sw;

	pr();
	copy_to_buf(info_485, relay_buf, sizeof(relay_buf));
	pr();
	len = send_and_recv_data(info_485, (char *)rbuf, 32);
	pr();

       	if(len < 0){
		printf("send error\n");
		return -1;
	}
	pr();
	 crc = ModBusCRC(rbuf, len - 2);
	pr();
	 if (crc != ((rbuf[len - 2] << 8) + rbuf[len - 1])){
	 	return -1;
	 }
	return 0;	
}

unsigned int modbus_get_temperature()
{
	return modbus_info.temperature;
}

int interval_time = 5;
int interval_value = 0;
int red = 1;
int modbus_callback(struct send_info *info_485, struct send_info *info_232)
{
	int tem;
	pr();
	if (interval_value < interval_time){
	pr();
		interval_value++;
	}else{
	pr();
		if ((tem = get_temperature(info_485)) > 0){
			modbus_info.temperature = tem;
		}
	printf("temperature = %d\n", tem);
		
	pr();
		tem = modbus_info.temperature;
		temperature_update_curve(info_232, (unsigned int)tem);
	pr();
		interval_value = 0;
		if((unsigned int )tem > modbus_info.warn_max || (unsigned int)tem < modbus_info.warn_min){
	pr();
			set_warn_icon(info_232, red);
			red = 0 ? 1 : red > 0;	
		}

		if ((unsigned int)tem >= modbus_info.extremity){
			modbus_info.cooling_score = COOLING_SCORE_MAX;	
			modbus_info.heating_score = 0;	
	pr();
		}
	}
	pr();
	set_relay(info_485);
	pr();
	return 0;
}

void modbus_update_warn_vaules(struct send_info *info_232, unsigned int max, unsigned int min)
{
	if (max)
		max = modbus_info.warn_max;
	if (min) 
		min = modbus_info.warn_min;

	if (modbus_info.warn_max != max || modbus_info.warn_min != min){
		if(temperature_draw_warn(info_232, max, min) < 0)
			return;
		modbus_info.warn_max = max;
		modbus_info.warn_min = min;
	}
	
}

void modbus_update_heating_score(unsigned int score)
{
	modbus_info.heating_score = score ? HEATING_SCORE_MAX : score < HEATING_SCORE_MAX;
}

void modbus_update_cooling_score(unsigned int score)
{
	modbus_info.cooling_score = score ? COOLING_SCORE_MAX : score < COOLING_SCORE_MAX;
}
