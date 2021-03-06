#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/select.h>
#include <unistd.h>
#include <time.h>

#include "vgus.h"
#include "usr-410s.h"

#define pr() printf("%s %s %d\n", __FILE__, __func__, __LINE__)

struct temperature_screen *t_screen;
struct curve_frame temperature_curve_frame;
struct xenomai_screen *x_screen;
struct curve_frame xenomai_curve_frame;

static int  write_vars(char *buf, int len, unsigned short addr, unsigned short *src) 
{
	set_var_head(buf, addr);
	set_var_len(buf, (len << 1) + 0x03); 
       	set_var_cmd(buf, 0x82); 
	int i; 
	for (i = 0; i < len; i++){ 
		set_value((buf + 6 + i * 2), src[i]);
	}
	return (len * 2 + 6);
}

static int  write_text_var(char *buf, int len, unsigned short addr, char *str) 
{
	set_var_head(buf, addr);
	set_var_len(buf, (len  + 0x03));
        set_var_cmd(buf, 0x82);
        memcpy(buf + 6, str, len);
	return (len + 6);
}

static int  write_curve_values(char *buf, int len, unsigned short channel, unsigned short *src) 
{
	buf[0] = h_16(frame_heafer); 				     
	buf[1] = l_16(frame_heafer);
	buf[4] = 1 << channel; 
	set_var_len(buf, len * 2 + 2);
	set_var_cmd(buf, 0x84);
	int i; 
	for (i = 0; i < len; i++){
		set_value(buf + 5 + i * 2, src[i]);
	}
	return len * 2 + 5;
}

static int  write_regs(char *buf, char addr, char *chars, int len) 
{
	set_reg_head(buf, addr);
	set_reg_len(buf, len + 2);
	set_reg_cmd(buf, 0x80);
	memcpy(buf + 5, chars, len);
	return len + 5;
}

static int  write_regs_short(char *buf, char addr, unsigned short data)
{ 
	char temp[2];
	temp[0] = h_16(data);
       	temp[1] = l_16(data);
       	write_regs(buf, addr, temp, 2);
	return 7;
}

static int  read_regs(char *buf, char addr, int len)
{
		set_reg_head(buf, addr);
		set_reg_cmd(buf, 0x81);
		*(buf + 5) = (char)(len & 0xFF);
		set_reg_len(buf, 3);
		return 6;
}

static void set_title_var(struct send_info *info, struct text_variable  *text, char *title)
{
	int len;
	char buf[32];
	len = strlen(title);
        len = write_text_var(buf, len, text->variable_addr, title);
	copy_to_buf(info, buf, len);

}

static void set_grid(struct send_info *info, struct grid_frame *grid)
{
	char buf[255] = {0};
	int len = 0;
	struct curve_frame * curve = grid->curve_frame; 
	unsigned short color0 = grid->color;

	write_wire_header(buf, grid->variable_addr);
	unsigned short y = curve->apex[1] + curve->width;
	unsigned short x = curve->apex[0];
	unsigned short interval = grid->interval;
	int i = 0, j;
	for (j =0; j < grid->number; j++){
		y -= interval;
		write_pixel_pair_n(buf, x, y, x + curve->length, y, color0, i++);
	}
	len = i * 10 + 10;
	set_var_len(buf, (len - 3));
	set_graph_num(buf, i);
	copy_to_buf(info, buf, len);
		
} 
static void set_axis(struct send_info *info, struct axis_frame *axis)
{
	char buf[255] = {0};
	int len = 0;
	int i = 0;
	struct curve_frame * curve = axis->curve_frame; 
	unsigned short color = axis->color;
	write_wire_header(buf, axis->variable_addr);
	unsigned short x0 = curve->apex[0], y0 = curve->apex[1], 
		 length = curve->length, width = curve->width,
		 x1 = x0 + length,
		 x2 = x0, y2 = y0 + width,
		 x3 = x1, y3 = y2;
	/**
	 *  (x0, y0)    (x1, y1)
	 * 
	 *
	 *  (x2, y2)    (x3, y3)
	 */
	
	y0 -= axis->grid->interval / 2;

	write_pixel_pair_n(buf, x2, y2, x0, y0, color, i++);
	write_pixel_pair_n(buf, x2 + 1, y2, x0 + 1, y0, color, i++);

	write_pixel_pair_n(buf, x2, y2, x3, y3, color, i++);
	write_pixel_pair_n(buf, x2, y2 + 1, x3, y3 + 1, color, i++);

	write_pixel_pair_n(buf, x0, y0, x0 - 10, y0 + 10, color, i++);
	write_pixel_pair_n(buf, x0, y0, x0 - 9, y0 + 10, color, i++);
	write_pixel_pair_n(buf, x0 + 1, y0, x0 + 10, y0 + 10, color, i++);
	write_pixel_pair_n(buf, x0 + 1, y0, x0 + 9, y0 + 10, color, i++);

	write_pixel_pair_n(buf, x3, y3, x3 - 10, y3 - 10, color, i++);
	write_pixel_pair_n(buf, x3, y3, x3 - 10, y3 - 9, color, i++);
	write_pixel_pair_n(buf, x3, y3 + 1, x3 - 10, y3 + 10, color, i++);
	write_pixel_pair_n(buf, x3, y3 + 1, x3 - 10, y3 + 9, color, i++);
	
	len = i * 10 + 10;
	set_var_len(buf, (len - 3));
	set_graph_num(buf, i);
	copy_to_buf(info, buf, len);
		
} 
static void reset_axis_attributes(struct send_info *info, struct axis_values *axis_v, int offset, unsigned short dat)
{
	char buf[255] = {0};
	int i = 0;

	for (i = 0; i < axis_v->number; i++){
		write_var(buf + 8 * i, axis_v->describe_addr + offset + i * axis_v->interval_addr, dat);	
	}

	copy_to_buf(info, buf,  8 * axis_v->number);


}
static void set_axis_values(struct send_info *info, struct axis_values *axis_v)
{
	char buf[255] = {0};

	int i;
	unsigned short value = axis_v->init;
	unsigned short addr = axis_v->variable_addr;
	for (i = 0; i < axis_v->number; i++){
		write_var((buf + i * 8), addr, value);
		value += axis_v->interval;
		addr += axis_v->interval_addr;
	}
	
	copy_to_buf(info, buf, 8 * axis_v->number);
		
} 

static void up_vernier(struct vernier *vernier, unsigned short n)
{
	if (vernier->vernier > 0xffff)
		return ;
	vernier->vernier += n;
}

//static int set_curve_values(struct send_info *info, unsigned short *buffer, int len, int channel)
//{
//	char buf[255] = {0};
//	int length  = 250 ? len : len > 250;
//	write_curve_values(buf, length, channel, buffer);
//	copy_to_buf(info, buf, (length * 2 + 5));
//	return length;

//}

static int set_curve_value(struct send_info *info, unsigned short data, int channel)
{
	char buf[16] = {0};
	write_curve_value(buf, channel, data);
	copy_to_buf(info, buf, 7);
	return 1;

}

static void set_vernier_value(struct send_info *info,struct xenomai_screen *xenomai, unsigned short data)
{
	unsigned short x, y;
	int len = 0;
	struct realtime_curve *curve= &xenomai->curve;
	float y1;  
        char buf[32] = {0};
	if ( xenomai->vernier.vernier > curve->curve_frame->length / curve->x_interval){
		x = ( curve->curve_frame->apex[0] + curve->curve_frame->length);
	}
	else {
		x = (xenomai->vernier.vernier - 1) * (curve->x_interval) + curve->curve_frame->apex[0];
	}

//	y = (data & 0xffff - Y_min) / ((Y_max - Y_min) / Y_hight_pixel) ;
	y1 = (((float)(data & 0xffff ) - curve->y_min) * (curve->curve_frame->width_valid)) / (curve->y_max - curve->y_min);
	y =curve->curve_frame->width - (unsigned short)y1 + curve->curve_frame->apex[1];
	x -= 8;
	len = 8;
	write_var(buf, xenomai->vernier.var.variable_addr, data);
	len += 10;	
	unsigned short temp[2];
	temp[0] = x; temp[1] = y;
	write_vars((buf + 8),2, (xenomai->vernier.var.describe_addr + 0x01), temp );
	copy_to_buf(info, buf, len);	
}

void xenomai_update_curve(struct send_info *info, unsigned int data)
{
	set_curve_value(info, data, x_screen->curve.channel);

	up_vernier(&x_screen->vernier, 1);
	set_vernier_value(info, x_screen, data);
} 
unsigned int sec = 0;
void temperature_update_curve(struct send_info *info, unsigned int data)
{
	char buf[16];
	unsigned short dat = (unsigned short)(data & 0xffff);
	if (sec & 0x01){
		set_curve_value(info, data, t_screen->curve.channel);
	}
	sec++;
	int len = write_var(buf, t_screen->temp.variable_addr, dat);
	copy_to_buf(info, buf, len);
}

int temperature_draw_warn(struct send_info *info, unsigned int max, unsigned int min)
{
	char buf[64];
	int len = 0;
	struct realtime_curve *curve = &(t_screen->curve); 
	
	if (max > curve->y_max + 100 || min < curve->y_min){
		return -1;
	}
	unsigned short dat = (unsigned short)(max & 0xffff);
	len = write_var(buf, t_screen->warn_max.variable_addr, dat);
	copy_to_buf(info, buf, len);
	dat = (unsigned short)(min & 0xffff);
	len = write_var(buf, t_screen->warn_min.variable_addr, dat);
	copy_to_buf(info, buf, len);
	dat = ((unsigned short)(min & 0xffff) + (unsigned short)(max & 0xffff)) / 2;
	len = write_var(buf, t_screen->target.variable_addr, dat);
	copy_to_buf(info, buf, len);
	dat -= (unsigned short)(min & 0xffff);
	len = write_var(buf, t_screen->offset.variable_addr, dat);
	copy_to_buf(info, buf, len);
//	float y1;
//	struct warn_frame *warn = &(t_screen->warn); 
//	struct curve_frame *curve_frame = warn->curve_frame;
//	struct realtime_curve *curve = &(t_screen->curve); 
//	unsigned short y;
//	unsigned short x = curve_frame->apex[0];
//	int i;
//	
//	if (max > curve->y_max || min < curve->y_min)
//		return -1;
//	
//	write_wire_header(buf, warn->variable_addr);
//
//	
//	y1 = (((float)(min & 0xffff ) - curve->y_min) * (curve_frame->width_valid)) / (curve->y_max - curve->y_min);
//	y = curve_frame->width - (unsigned short)y1 + curve_frame->apex[1];
//	write_pixel_pair_n(buf, x, y, x + curve_frame->length, y, warn->color_min, i++);
//
//	y1 = (((float)(max & 0xffff ) - curve->y_min) * (curve_frame->width_valid)) / (curve->y_max - curve->y_min);
//	y = curve_frame->width - (unsigned short)y1 + curve_frame->apex[1];
//	write_pixel_pair_n(buf, x, y, x + curve_frame->length, y, warn->color_max, i++);
//	len = i * 10 + 10;
//	set_var_len(buf, (len - 3));
//	set_graph_num(buf, i);
//	copy_to_buf(info, buf, len);
	
	return 0;	
}

static void realtime_curve_init(struct send_info *info, struct realtime_curve *curve)
{
        char buf[64] = {0};
	int len;
	unsigned short data = (curve->channel << 8) | curve->x_interval;
	len = write_var(buf, curve->describe_addr + 0x09, data); 
	unsigned short nul_y = 0;
	nul_y = ((curve->curve_frame->width_valid) << 8) / (curve->y_max - curve->y_min);  
	len += write_var(buf + len, curve->describe_addr + 0x08, nul_y);

	unsigned short y_centre = curve->curve_frame->width_valid / 2 + curve->curve_frame->apex[1];
	len += write_var(buf + len, curve->describe_addr + 0x05, y_centre);

	unsigned short y_centre_v = (curve->y_max - curve->y_min) / 2 + curve->y_min;
	len += write_var(buf + len, curve->describe_addr + 0x06, y_centre_v);
	
	copy_to_buf(info, buf, len);	
}

void curve_clear_data(struct send_info *info, struct realtime_curve *curve )  // ch = 0 : all ch; 
{
	char buf[] = {0xa5, 0x5a, 0x03, 0x80, 0xeb, 0x55};
	buf[5] += curve->channel + 1;
	copy_to_buf(info, buf, 6);
}

void switch_screen(struct send_info *info,  unsigned short screen_id) 
{
	char buf[16];
	int len;
	len = write_regs_short(buf, 0x03, screen_id);
	copy_to_buf(info, buf, len);
}
void set_breath_led(struct send_info *info, int breach_led)
{
	char buf[16];
	int len;
	unsigned short v = breach_led;
	len = write_var(buf, t_screen->breach_led.variable_addr, v);
	copy_to_buf(info, buf, len);
}

void set_touch_warn(struct send_info *info, unsigned short addr, unsigned short val)
{
	char buf[16];
	int len;
	len = write_var(buf, addr, val);
	copy_to_buf(info, buf, len);
	send_data(info);
}

void set_warn_icon(struct send_info *info, int red)
{
	char buf[16];
	int len;
	unsigned short v = red;
	len = write_var(buf, t_screen->warn_icon.variable_addr, v);
	copy_to_buf(info, buf, len);
}

int parse_read_reg(char *buf, char addr, int len)
{
	char *p_index = buf;
	while(len > 0){
		if(p_index[0] != h_16(frame_heafer)){
			p_index++;
			len--;
			continue;
		}
		if(p_index[1] != l_16(frame_heafer)){
			p_index++;
			len--;
			continue;
		}
		if(p_index[3] != (char)0x81){
			p_index++;
			len--;
			continue;
		}

		if(len < p_index[2] + 3)
			return 0;
		if(p_index[4] == (char)addr){
			return (p_index - buf) + 6;
		}
		
		len -= p_index[2] + 3;
		p_index += p_index[2] + 3;

	}
	return 0;
}

unsigned int get_touch_coord(struct send_info *info)
{
	char buf[16];
	int len;
	int index = 0;
	unsigned char *p;
	len = read_regs(buf, 0x05, 6);
	copy_to_buf(info, buf, len);
	len = send_and_recv_data(info, buf, 16);
	if (strncmp(buf, "lift", 4) == 0){
		return ~0;
	}
	if (!(index = parse_read_reg(buf, 0x05, len)))
		return 0;

	if (buf[index] != 0x5a)
		return 0;
	
	if (buf[index + 1] == 0x02)  // lift 
		return ~0;
	p = (unsigned char *)(buf + index + 2);
	return p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];

}

int temperature_screen_init()
{
	t_screen = malloc(sizeof(struct temperature_screen));
	if (!t_screen){
		return -1;
	}
	
	temperature_curve_frame.length = 700;
	temperature_curve_frame.width = 240;
	temperature_curve_frame.apex[0] = 55;
	temperature_curve_frame.apex[1] = 200;
	temperature_curve_frame.width_valid = 240;

	t_screen->temp.describe_addr = 0x1400;
	t_screen->temp.variable_addr = 0x5400;
	t_screen->warn_max.describe_addr = 0x1420;
	t_screen->warn_max.variable_addr = 0x5420;
	t_screen->warn_min.describe_addr = 0x1440;
	t_screen->warn_min.variable_addr = 0x5440;
	t_screen->warn_icon.describe_addr = 0x1000;
	t_screen->warn_icon.variable_addr = 0x5000;
	
	t_screen->target.describe_addr = 0x1620;
	t_screen->target.variable_addr = 0x5620;
	t_screen->offset.describe_addr = 0x1640;
	t_screen->offset.variable_addr = 0x5640;
	
	t_screen->curve.describe_addr = 0x1020;
	t_screen->curve.curve_frame = &temperature_curve_frame;
	t_screen->curve.channel = 0x02;
	t_screen->curve.y_max = 400;
	t_screen->curve.y_min = 250;
	t_screen->curve.x_interval = 1;
	t_screen->curve.color = 0xF810;

	t_screen->warn.variable_addr = 0x5460;
	t_screen->warn.describe_addr = 0x1460;
	t_screen->warn.color_max = 0xF800;
	t_screen->warn.curve_frame = &temperature_curve_frame;
	t_screen->warn.color_min = 0x801F;

	t_screen->grid.describe_addr = 0x13c0;
	t_screen->grid.variable_addr = 0x53c0;
	t_screen->grid.number = 6;
	t_screen->grid.interval = 40;
        t_screen->grid.curve_frame = &temperature_curve_frame;
	t_screen->grid.color = 0xFC00;

	t_screen->breach_led.variable_addr = 0x5510;

	t_screen->x_axis.number = 7;
	t_screen->x_axis.init = 250;
	t_screen->x_axis.interval = 25;
        t_screen->x_axis.variable_addr = 0x5180;
	t_screen->x_axis.interval_addr = 0x20;
	t_screen->x_axis.describe_addr = 0x1180;

	t_screen->y_axis.number = 11;
	t_screen->y_axis.init = 1200;
	t_screen->y_axis.interval = -120;
        t_screen->y_axis.variable_addr = 0x5260;
	t_screen->y_axis.interval_addr = 0x20;
	t_screen->y_axis.describe_addr = 0x1260;

	t_screen->axis.describe_addr = 0x13e0;		
	t_screen->axis.variable_addr = 0x53e0;
	t_screen->axis.curve_frame = &temperature_curve_frame;
	t_screen->axis.color = 0x4008;
	t_screen->axis.grid = &t_screen->grid;

	t_screen->touch_warn.variable_addr = 0x5520;
	t_screen->screen_id = 0x01;

	return 0;
}

int xenomai_screen_init(){
	x_screen = malloc(sizeof(struct xenomai_screen));
	if (!x_screen){
		free(t_screen);
		return -1;
	}
	xenomai_curve_frame.length = 700;
	xenomai_curve_frame.width = 300;
	xenomai_curve_frame.apex[0] = 55;
	xenomai_curve_frame.apex[1] = 140;
	xenomai_curve_frame.width_valid = 300;

	x_screen->vernier.var.variable_addr = 0x42e0;
	x_screen->vernier.var.describe_addr = 0x0300;
	x_screen->vernier.vernier = 0;
	x_screen->vernier.curve_frame = &xenomai_curve_frame;
	
	x_screen->title.describe_addr = 0x0000;
	x_screen->title.variable_addr = 0x4000;

	x_screen->curve.describe_addr = 0x0020;
	x_screen->curve.curve_frame = &xenomai_curve_frame;
	x_screen->curve.channel = 0x01;
	x_screen->curve.y_max = 3000;
	x_screen->curve.y_min = 500;
	x_screen->curve.x_interval = 20;
	x_screen->curve.color = 0xF810;

	x_screen->grid.describe_addr = 0x0440;
	x_screen->grid.variable_addr = 0x4500;
	x_screen->grid.number = 10;
	x_screen->grid.interval = 30;
        x_screen->grid.curve_frame = &xenomai_curve_frame;
	x_screen->grid.color = 0xFC00;
	
	x_screen->touch_warn.variable_addr = 0x5530;

	x_screen->x_axis.number = 11;
	x_screen->x_axis.init = 500;
	x_screen->x_axis.interval = 250;
        x_screen->x_axis.variable_addr = 0x4020;
	x_screen->x_axis.interval_addr = 0x20;
	x_screen->x_axis.describe_addr = 0x0040;
	x_screen->axis.grid = &x_screen->grid;

	x_screen->y_axis.number = 11;
	x_screen->y_axis.init = 350;
	x_screen->y_axis.interval = -35;
        x_screen->y_axis.variable_addr = 0x4180;
        x_screen->y_axis.describe_addr = 0x01A0;
	x_screen->y_axis.interval_addr = 0x20;

	x_screen->axis.describe_addr = 0x0320;		
	x_screen->axis.variable_addr = 0x4300;
	x_screen->axis.curve_frame = &xenomai_curve_frame;
	x_screen->axis.color = 0x4008;

	x_screen->screen_id = 0x02;
	return 0;
}
void vgus_init(struct send_info *info)
{
	xenomai_screen_init();
	temperature_screen_init();
	set_title_var(info, &x_screen->title, "Xenomai Interrupt Latency");
	curve_clear_data(info, &x_screen->curve);
	send_data(info);
	curve_clear_data(info, &t_screen->curve);
	send_data(info);
	realtime_curve_init(info, &t_screen->curve);
	realtime_curve_init(info, &x_screen->curve);
	send_data(info);
	set_grid(info, &x_screen->grid);
	set_grid(info, &t_screen->grid);
	send_data(info);
	set_axis(info, &x_screen->axis);
	set_axis(info, &t_screen->axis);
	send_data(info);
	set_axis_values(info, &x_screen->x_axis);
	set_axis_values(info, &x_screen->y_axis);
	send_data(info);
	set_axis_values(info, &t_screen->x_axis);
	set_axis_values(info, &t_screen->y_axis);
	reset_axis_attributes(info, &t_screen->y_axis, 6, 0 << 8);
	reset_axis_attributes(info, &t_screen->x_axis, 6, 1 << 8);
	send_data(info);
	temperature_draw_warn(info, 425, 255);
	switch_screen(info, t_screen->screen_id);
	send_data(info);
}
