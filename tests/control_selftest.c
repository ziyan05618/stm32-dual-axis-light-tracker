#include "control.h"
#include "tracker_config.h"
#include <stdint.h>
volatile uint32_t checks;
#define CHECK(e) do { ++checks; if(!(e)) return __LINE__; } while(0)
int selftest(void)
{
 axis_t a; uint8_t out=0;
 axis_init(&a,32,1);
 CHECK(a.position==0 && a.target==0 && a.active==0);
 axis_move(&a,16);
 CHECK(a.target==16);
 CHECK(axis_tick(&a,1,0)==0 && a.target==0);
 axis_move(&a,16);
 for(uint32_t t=2;t<1000;t++)out=axis_tick(&a,t,1);
 CHECK(a.position==16 && a.target==16 && out!=0);
 CHECK(axis_tick(&a,1000,0)==0 && a.active==0);
 CHECK(axis_tick(&a,1001,1)==0);
 axis_jog(&a,1);
 for(uint32_t t=1002;t<2000;t++)axis_tick(&a,t,1);
 CHECK(a.position==32);
 axis_jog(&a,-1);
 for(uint32_t t=2000;t<3000;t++)axis_tick(&a,t,1);
 CHECK(a.position==-32);
 axis_jog(&a,0);
 CHECK(a.target==-32 && a.jog==0);
 axis_init(&a,32,1);
 axis_move(&a,9999);CHECK(a.target==32);
 axis_move(&a,-9999);CHECK(a.target==0);
 axis_move(&a,-9999);CHECK(a.target==-32);
 axis_init(&a,32,1);axis_move(&a,3);
 CHECK(axis_tick(&a,8,1)==0 && a.position==0);
 CHECK(axis_tick(&a,9,1)==3 && a.position==1);
 CHECK(axis_tick(&a,16,1)==3 && a.position==1);
 CHECK(axis_tick(&a,17,1)==2 && a.position==2);
 axis_stop(&a);axis_move(&a,-1);
 CHECK(axis_tick(&a,25,1)==2 && a.position==2);
 CHECK(axis_tick(&a,26,1)==3 && a.position==1);
 axis_init(&a,32,-1);axis_move(&a,1);
 CHECK(axis_tick(&a,9,1)==9 && a.position==1);
 axis_init(&a,32,1);a.last_step=UINT32_MAX-4;axis_move(&a,1);
 CHECK(axis_tick(&a,3,1)==0);
 CHECK(axis_tick(&a,4,1)==3 && a.position==1);
 /* Return from both soft boundaries; target 0 must never be clamped outward. */
 for(int sign=-1;sign<=1;sign+=2){
  axis_init(&a,32,sign);a.position=sign*32;a.target=a.position;a.phase=5u;
  axis_home(&a);CHECK(a.target==0&&a.jog==0&&a.min_interval==HOME_STEP_MS);
  int32_t before=a.position;uint32_t last=0u;unsigned steps=0u;
  for(uint32_t t=0;t<1000u;++t){
   axis_tick(&a,t,1);
   if(a.position!=before){
    CHECK(a.position==before-sign);CHECK(t-last>=HOME_STEP_MS);
    last=t;before=a.position;++steps;
   }
  }
  CHECK(a.position==0&&steps==32u&&a.active);
  axis_stop(&a);CHECK(a.min_interval==STEP_FAST_MS);
 }
 axis_init(&a,32,1);a.position=30;axis_home(&a);
 for(uint32_t t=0;t<90u;++t)axis_tick(&a,t,1);
 int32_t interrupted=a.position;CHECK(interrupted>0&&interrupted<30);
 CHECK(axis_tick(&a,90u,0)==0u&&a.target==interrupted);
 for(uint32_t t=91u;t<300u;++t)axis_tick(&a,t,0);
 CHECK(a.position==interrupted); /* Stop cancels, it must never resume itself. */
 CHECK(track_direction(600,500,0)==1);
 CHECK(track_direction(500,600,0)==-1);
 CHECK(track_direction(540,500,0)==1);
 CHECK(track_direction(539,500,0)==0);
 CHECK(track_direction(521,500,1)==1);
 CHECK(track_direction(520,500,1)==0);
 CHECK(track_direction(479,500,-1)==-1);
 CHECK(track_direction(480,500,-1)==0);
 CHECK(track_direction(0,0,1)==0);
 CHECK(track_direction(19,0,1)==0);
 CHECK(track_direction(500,500,-1)==0);
 CHECK(light_from_adc(0)==1000);
 CHECK(light_from_adc(4095)==0);
 CHECK(light_from_adc(65535)==0);
 uint16_t previous=1000;
 for(unsigned r=0;r<=4095;r++){
  uint16_t light=light_from_adc((uint16_t)r);
  CHECK(light<=previous && light<=1000);previous=light;
 }
 CHECK(battery_from_adc(0)==0);
 CHECK(battery_from_adc(4095)==6600);
 CHECK(battery_from_adc(65535)==6600);
 CHECK(battery_from_adc(2048)>=3299 && battery_from_adc(2048)<=3301);
 CHECK(voltage_percent(0)==0 && voltage_percent(3000)==0);
 CHECK(voltage_percent(3600)==50);
 CHECK(voltage_percent(4200)==100 && voltage_percent(65535)==100);
 previous=0;
 for(unsigned mv=0;mv<=6600;mv++){uint8_t pct=voltage_percent((uint16_t)mv);CHECK(pct>=previous && pct<=100);previous=pct;}
 return 0;
}
