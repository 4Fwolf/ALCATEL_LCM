

#include <linux/string.h>
#if defined(BUILD_UBOOT)
#include <asm/arch/mt6577_gpio.h>
#else
#include <mach/mt6577_gpio.h>
#endif

#include "lcm_drv.h"

// ---------------------------------------------------------------------------
//  Local Constants
// ---------------------------------------------------------------------------

#define FRAME_WIDTH  										(480)
#define FRAME_HEIGHT 										(800)

#define REGFLAG_DELAY             							0XFE
#define REGFLAG_END_OF_TABLE      							0xFF   // END OF REGISTERS MARKER

// ---------------------------------------------------------------------------
//  Local Variables
// ---------------------------------------------------------------------------

static LCM_UTIL_FUNCS lcm_util = {0};

#define SET_RESET_PIN(v)    								(lcm_util.set_reset_pin((v)))

#define UDELAY(n) 											(lcm_util.udelay(n))
#define MDELAY(n) 											(lcm_util.mdelay(n))

#define LCM_ID       (0x69)


// ---------------------------------------------------------------------------
//  Local Functions
// ---------------------------------------------------------------------------

#define dsi_set_cmdq_V2(cmd, count, ppara, force_update)	lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update)		lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd)										lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums)					lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg											lcm_util.dsi_read_reg()
#define read_reg_v2(cmd, buffer, buffer_size)   			lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

struct LCM_setting_table {
    unsigned cmd;
    unsigned char count;
    unsigned char para_list[64];
};


// ---------------------------------------------------------------------------
//  LCM Driver Implementations
// ---------------------------------------------------------------------------

static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
    memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}


static void lcm_get_params(LCM_PARAMS *params)
{
		unsigned int div2_real=0;
		unsigned int cycle_time = 0;
		unsigned int ui = 0;
		unsigned int hs_trail_m, hs_trail_n;
		#define NS_TO_CYCLE(n, c)	((n) / c + (( (n) % c) ? 1 : 0))

		memset(params, 0, sizeof(LCM_PARAMS));

		params->type   = LCM_TYPE_DSI;

		params->width  = FRAME_WIDTH;
		params->height = FRAME_HEIGHT;

		// enable tearing-free
		params->dbi.te_mode 			= LCM_DBI_TE_MODE_VSYNC_ONLY;
		params->dbi.te_edge_polarity		= LCM_POLARITY_RISING;

		params->dsi.mode   = CMD_MODE;

		// DSI
		/* Command mode setting */
		params->dsi.LANE_NUM				= LCM_TWO_LANE;
		//The following defined the fomat for data coming from LCD engine.
		params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
		params->dsi.data_format.trans_seq   = LCM_DSI_TRANS_SEQ_MSB_FIRST;
		params->dsi.data_format.padding     = LCM_DSI_PADDING_ON_LSB;
		params->dsi.data_format.format      = LCM_DSI_FORMAT_RGB888;

		// Highly depends on LCD driver capability.
		params->dsi.packet_size=256;

		// Video mode setting
		params->dsi.PS=LCM_PACKED_PS_24BIT_RGB888;

		params->dsi.word_count=480*3;
		params->dsi.vertical_sync_active=2;
		params->dsi.vertical_backporch=2;
		params->dsi.vertical_frontporch=2;
		params->dsi.vertical_active_line=800;

		params->dsi.line_byte=2180;		// 2256 = 752*3
		params->dsi.horizontal_sync_active_byte=26;
		params->dsi.horizontal_backporch_byte=206;
		params->dsi.horizontal_frontporch_byte=206;
		params->dsi.rgb_byte=(480*3+6);

		params->dsi.horizontal_sync_active_word_count=20;
		params->dsi.horizontal_backporch_word_count=200;
		params->dsi.horizontal_frontporch_word_count=200;

		// Bit rate calculation
		params->dsi.pll_div1=38;		// fref=26MHz, fvco=fref*(div1+1)	(div1=0~63, fvco=500MHZ~1GHz)
		params->dsi.pll_div2=1;			// div2=0~15: fout=fvo/(2*div2)

		div2_real=params->dsi.pll_div2 ? params->dsi.pll_div2*0x02 : 0x1;
		cycle_time = (8 * 1000 * div2_real)/ (26 * (params->dsi.pll_div1+0x01));
		ui = (1000 * div2_real)/ (26 * (params->dsi.pll_div1+0x01)) + 1;

		hs_trail_m=params->dsi.LANE_NUM;
		hs_trail_n=NS_TO_CYCLE(((params->dsi.LANE_NUM * 4 * ui) + 60), cycle_time);

//		params->dsi.HS_TRAIL	= ((hs_trail_m > hs_trail_n) ? hs_trail_m : hs_trail_n) + 3;//min max(n*8*UI, 60ns+n*4UI)
		params->dsi.HS_TRAIL	= 20;
		params->dsi.HS_ZERO 	= NS_TO_CYCLE((115 + 6 * ui), cycle_time);//min 105ns+6*UI
		params->dsi.HS_PRPR 	= NS_TO_CYCLE((50 + 4 * ui), cycle_time);//min 40ns+4*UI; max 85ns+6UI
		// HS_PRPR can't be 1.
		if (params->dsi.HS_PRPR < 2)
			params->dsi.HS_PRPR = 2;

		params->dsi.LPX 		= NS_TO_CYCLE(200, cycle_time);//min 50ns

		params->dsi.TA_SACK 	= 1;
		params->dsi.TA_GET		= 5 * params->dsi.LPX;//5*LPX
		params->dsi.TA_SURE 	= 3 * params->dsi.LPX / 2;//min LPX; max 2*LPX;
		params->dsi.TA_GO		= 4 * params->dsi.LPX;//4*LPX

		params->dsi.CLK_TRAIL	= NS_TO_CYCLE(70, cycle_time);//min 60ns
		// CLK_TRAIL can't be 1.
		if (params->dsi.CLK_TRAIL < 2)
			params->dsi.CLK_TRAIL = 2;
		params->dsi.CLK_ZERO	= NS_TO_CYCLE((300), cycle_time);//min 300ns-38ns
		params->dsi.LPX_WAIT	= 1;
		params->dsi.CONT_DET	= 0;

		params->dsi.CLK_HS_PRPR = NS_TO_CYCLE((38 + 95) / 2, cycle_time);//min 38ns; max 95ns


}


static struct LCM_setting_table lcm_initialization_setting[] = {

	/*
	Note :

	Data ID will depends on the following rule.

		count of parameters > 1	=> Data ID = 0x39
		count of parameters = 1	=> Data ID = 0x15
		count of parameters = 0	=> Data ID = 0x05

	Structure Format :

	{DCS command, count of parameters, {parameter list}}
	{REGFLAG_DELAY, milliseconds of time, {}},

	...

	Setting ending by predefined flag

	{REGFLAG_END_OF_TABLE, 0x00, {}}
	*/


	{0xB9,	3,	{0xFF, 0x83, 0x69}},
	{REGFLAG_DELAY, 10, {}},

	{0xB0,	2,	{0x01, 0x0B}},
	{REGFLAG_DELAY, 10, {}},

	{0xB2,	15,	{0x00, 0x20, 0x05, 0x05,
				 0x70, 0x00, 0xFF, 0x00,
				 0x00, 0x00, 0x00, 0x03,
				 0x03, 0x00, 0x01}},
	{REGFLAG_DELAY, 10, {}},

	{0xB4, 	5,	{0x00, 0x1D, 0x5F, 0x0E,
				 0x06}},
	{REGFLAG_DELAY, 10, {}},

	{0xD5,	26, {0x00, 0x05, 0x03, 0x00,
				 0x01, 0x09, 0x10, 0x80,
				 0x37, 0x37, 0x20, 0x31,
				 0x46, 0x8a, 0x57, 0x9b,
				 0x20, 0x31, 0x46, 0x8a,
				 0x57, 0x9b, 0x07, 0x0F,
				 0x02, 0x00}},
	{REGFLAG_DELAY, 10, {}},

	{0xB1,	19,	{0x01, 0x00, 0x34, 0x06,
				 0x00, 0x0e, 0x0e, 0x1A,
				 0x21, 0x3F, 0x3F, 0x07,
				 0x23, 0x01, 0xE6, 0xE6,
				 0xE6, 0xE6, 0xE6}},
	{REGFLAG_DELAY, 10, {}},


	{0x3A,	1,	{0x77}},
	{0xCC,	1,	{0x02}},

	{0xB6,	2,	{0x21, 0x21}},      ///apply it as ESD indication
	{REGFLAG_DELAY, 10, {}},

	// ENABLE FMARK
	{0x44,	2,	{((FRAME_HEIGHT/2)>>8), ((FRAME_HEIGHT/2)&0xFF)}},
	{0x35,	1,	{0x00}},

	// SET GAMMA
	{0xE0,	34,	{0x00, 0x01, 0x03, 0x2B,
				 0x33, 0x3F, 0x0D, 0x30,
				 0x06, 0x0B, 0x0D, 0x10,
				 0x13, 0x11, 0x13, 0x11,
				 0x17, 0x00, 0x01, 0x03,
				 0x2B, 0x33, 0x3F, 0x0D,
				 0x30, 0x06, 0x0B, 0x0D,
				 0x10, 0x13, 0x11, 0x13,
				 0x11, 0x17}},
	{REGFLAG_DELAY, 10, {}},

	{0xBA,	13,	{0x00, 0xA0, 0xC6, 0x00,
				 0x0A, 0x02, 0x10, 0x30,
				 0x6F, 0x02, 0x11, 0x18,
				 0x40}},

	// Note
	// Strongly recommend not to set Sleep out / Display On here. That will cause messed frame to be shown as later the backlight is on.

	{0x53,	1,	{0x24}},
	{REGFLAG_DELAY, 10, {}},

	{0x11,	1,	{0x00}},
	{REGFLAG_DELAY, 120, {}},
	{0x29,	1,	{0x00}},

	// Setting ending by predefined flag
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};


static void push_table(struct LCM_setting_table *table, unsigned int count, unsigned char force_update)
{
	unsigned int i;

    for(i = 0; i < count; i++) {

        unsigned cmd;
        cmd = table[i].cmd;

        switch (cmd) {

            case REGFLAG_DELAY :
                MDELAY(table[i].count);
                break;

            case REGFLAG_END_OF_TABLE :
                break;

            default:
				dsi_set_cmdq_V2(cmd, table[i].count, table[i].para_list, force_update);
       	}
    }

}


static void lcm_init(void)
{
    SET_RESET_PIN(1);
    MDELAY(1);
    SET_RESET_PIN(0);
    MDELAY(10);
    SET_RESET_PIN(1);
    MDELAY(50);

    //init_lcm_registers();
    push_table(lcm_initialization_setting, sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table), 1);

}


static void lcm_suspend(void)
{
	//push_table(lcm_deep_sleep_mode_in_setting, sizeof(lcm_deep_sleep_mode_in_setting) / sizeof(struct LCM_setting_table), 1);
	unsigned int data_array[16];

	data_array[0]=0x00280500;
	dsi_set_cmdq(&data_array, 1, 1);
	//MDELAY(50);

	data_array[0]=0x00100500;
	dsi_set_cmdq(&data_array, 1, 1);
	MDELAY(150);
}


static void lcm_resume(void)
{
	//push_table(lcm_sleep_out_setting, sizeof(lcm_sleep_out_setting) / sizeof(struct LCM_setting_table), 1);
	//unsigned int data_array[16];

	//data_array[0]=0x00110500;
	//dsi_set_cmdq(&data_array, 1, 1);
	//MDELAY(150);

	//data_array[0]=0x00290500;
	//dsi_set_cmdq(&data_array, 1, 1);
	lcm_init();
}


static void lcm_update(unsigned int x, unsigned int y,
                       unsigned int width, unsigned int height)
{
	unsigned int x0 = x;
	unsigned int y0 = y;
	unsigned int x1 = x0 + width - 1;
	unsigned int y1 = y0 + height - 1;

	unsigned char x0_MSB = ((x0>>8)&0xFF);
	unsigned char x0_LSB = (x0&0xFF);
	unsigned char x1_MSB = ((x1>>8)&0xFF);
	unsigned char x1_LSB = (x1&0xFF);
	unsigned char y0_MSB = ((y0>>8)&0xFF);
	unsigned char y0_LSB = (y0&0xFF);
	unsigned char y1_MSB = ((y1>>8)&0xFF);
	unsigned char y1_LSB = (y1&0xFF);

	unsigned int data_array[16];

	data_array[0]= 0x00053902;
	data_array[1]= (x1_MSB<<24)|(x0_LSB<<16)|(x0_MSB<<8)|0x2a;
	data_array[2]= (x1_LSB);
	data_array[3]= 0x00053902;
	data_array[4]= (y1_MSB<<24)|(y0_LSB<<16)|(y0_MSB<<8)|0x2b;
	data_array[5]= (y1_LSB);
	data_array[6]= 0x002c3909;

	dsi_set_cmdq(data_array, 7, 0);

}


void lcm_setbacklight(unsigned int level)
{
	unsigned int data_array[16];

#if defined(BUILD_UBOOT)
        printf("%s,  \n", __func__);
#endif

	if(level > 255)
	    level = 255;

	data_array[0]= 0x00023902;
	data_array[1] =(0x51|(level<<8));
	dsi_set_cmdq(&data_array, 2, 1);

	#if BUILD_UBOOT
	printf("lcm_setbacklight level=%d\n",level);
	#else
	printk("lcm_setbacklight level=%d\n",level);
	#endif

}


void lcm_setpwm(unsigned int divider)
{
	// TBD
}


unsigned int lcm_getpwm(unsigned int divider)
{
	// ref freq = 15MHz, B0h setting 0x80, so 80.6% * freq is pwm_clk;
	// pwm_clk / 255 / 2(lcm_setpwm() 6th params) = pwm_duration = 23706
	unsigned int pwm_clk = 23706 / (1<<divider);
	return pwm_clk;
}

static unsigned int lcm_compare_id()
{
	unsigned int id = 0, id2 = 0;
	unsigned char buffer[2];

	unsigned int data_array[16];

	SET_RESET_PIN(1);  //NOTE:should reset LCM firstly
	MDELAY(10);
	SET_RESET_PIN(0);
	MDELAY(10);
	SET_RESET_PIN(1);
	MDELAY(120);


//*************Enable CMD2 Page1  *******************//
	data_array[0]=0x00043902;
	data_array[1]=0x6983FFB9;
	dsi_set_cmdq(&data_array, 2, 1);
	MDELAY(10);

	data_array[0] = 0x00023700;// read id return two byte,version and id
	dsi_set_cmdq(data_array, 1, 1);
	MDELAY(10);

	read_reg_v2(0xF4, buffer, 2);
	id = buffer[0]; //we only need ID
	id2= buffer[1]; //we test buffer 1

        return (LCM_ID == id)?1:0;
}

// ---------------------------------------------------------------------------
//  Get LCM Driver Hooks
// ---------------------------------------------------------------------------

LCM_DRIVER hx8369_dsi_6577_lcm_drv =
{
	.name			= "hx8369_dsi_6577",
        .set_util_funcs = lcm_set_util_funcs,
        .get_params     = lcm_get_params,
        .init           = lcm_init,
        .suspend        = lcm_suspend,
        .resume         = lcm_resume,
        .set_backlight	= lcm_setbacklight,
		//.set_pwm        = lcm_setpwm,
		//.get_pwm        = lcm_getpwm,
	.compare_id    = lcm_compare_id,
        .update         = lcm_update

};


