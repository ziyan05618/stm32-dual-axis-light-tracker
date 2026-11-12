#include "board.h"
#include "control.h"
#include "display.h"
#include "ui.h"
#include "position_store.h"

volatile uint32_t g_boot_marker;
volatile uint32_t g_ui_last_ms,g_ui_max_ms,g_ui_frames;
volatile uint32_t g_ui_chunk_last_ms,g_ui_chunk_max_ms;
volatile uint8_t g_enabled,g_mode_auto=1u,g_adc_fault;
volatile uint16_t g_adc_raw[5],g_light[4],g_battery_mv;
static volatile axis_t axes[2];
static int16_t light_offset[4];
static uint8_t sample_valid,button_mask;
static uint8_t ui_limit_attempts;
static int pan_tracking,tilt_tracking;
volatile uint8_t g_position_known,g_position_dirty,g_position_fault,g_returning,g_boot_home_pending;
static uint32_t home_started,boot_home_at,quiet_at;
static int32_t quiet_pan,quiet_tilt;

typedef struct {uint8_t candidate,stable,long_fired,held;uint32_t changed,pressed;} key_t;
static key_t keys[5];
static const unsigned key_pins[5]={KEY_LEFT_PIN,KEY_DOWN_PIN,KEY_RIGHT_PIN,KEY_UP_PIN,KEY_MODE_PIN};

void SysTick_Handler(void)
{
 ++g_millis;
 if(g_io_ready){uint8_t pan=axis_tick(&axes[0],g_millis,g_enabled);uint8_t tilt=axis_tick(&axes[1],g_millis,g_enabled);board_coils(pan,tilt);}
}
static void stop_motion(void)
{
 uint32_t lock=irq_lock();axis_stop(&axes[0]);axis_stop(&axes[1]);irq_restore(lock);
 pan_tracking=0;tilt_tracking=0;
}
static void disarm(void)
{
 uint32_t lock=irq_lock();g_enabled=0;axis_stop(&axes[0]);axis_stop(&axes[1]);board_coils(0,0);irq_restore(lock);
 pan_tracking=0;tilt_tracking=0;
 g_returning=0u;g_boot_home_pending=0u;
}
static int save_position(int clean)
{
 uint32_t lock=irq_lock();
 saved_position_t p={axes[0].position,axes[1].position,axes[0].phase,axes[1].phase,axes[0].phase_sign,axes[1].phase_sign};
 int ok=position_store_save(&p,clean);irq_restore(lock);
 if(!ok){g_position_known=0u;g_position_fault=1u;disarm();return 0;}
 g_position_dirty=(uint8_t)!clean;return 1;
}
static int can_move(unsigned i,int direction)
{
 return direction>0?axes[i].position<axes[i].limit:(direction<0&&axes[i].position> -axes[i].limit);
}
static int prepare_motion(void)
{
 /* Write-ahead invalidation must commit BEFORE the first possible step. */
 if(g_position_fault)return 0;
 return !g_position_known||g_position_dirty||save_position(0);
}
static int set_zero(void)
{
 if(g_enabled||g_returning)return 0;
 g_boot_home_pending=0u;
 uint32_t lock=irq_lock();axes[0].position=0;axes[1].position=0;axis_stop(&axes[0]);axis_stop(&axes[1]);irq_restore(lock);
 if(!save_position(1))return 0;
 g_position_known=1u;g_position_fault=0u;return 1;
}
static int begin_home(uint32_t now)
{
 if(!g_position_known||g_position_fault||g_adc_fault||!sample_valid)return 0;
 disarm();
 if(!axes[0].position&&!axes[1].position)return 1;
 if(!prepare_motion())return 0;
 uint32_t lock=irq_lock();axis_home(&axes[0]);axis_home(&axes[1]);
 g_returning=1u;home_started=now;g_enabled=1u;irq_restore(lock);
 return 1;
}
static void restore_position(void)
{
 saved_position_t p;int state=position_store_load(&p);
 if(state!=POSITION_MISSING){axes[0].phase_sign=p.pan_sign;axes[1].phase_sign=p.tilt_sign;}
 if(state!=POSITION_CLEAN)return;
 axes[0].position=p.pan;axes[0].target=p.pan;axes[0].phase=p.pan_phase;
 axes[1].position=p.tilt;axes[1].target=p.tilt;axes[1].phase=p.tilt_phase;
 g_position_known=1u;g_boot_home_pending=(uint8_t)(p.pan!=0||p.tilt!=0);
 boot_home_at=millis();
}
static void service_position(uint32_t now)
{
 if(g_boot_home_pending){
  if(button_mask){disarm();return;}
  if((uint32_t)(now-boot_home_at)>=HOME_START_DELAY_MS){g_boot_home_pending=0u;(void)begin_home(now);}
 }
 if(g_returning){
  if((uint32_t)(now-home_started)>=HOME_TIMEOUT_MS){disarm();g_position_known=0u;return;}
  if(!axes[0].position&&!axes[1].position){disarm();(void)save_position(1);}
  return;
 }
 uint32_t lock=irq_lock();int32_t pan=axes[0].position,tilt=axes[1].position;irq_restore(lock);
 if(pan!=quiet_pan||tilt!=quiet_tilt){quiet_pan=pan;quiet_tilt=tilt;quiet_at=now;}
 else if(g_position_known&&g_position_dirty&&(uint32_t)(now-quiet_at)>=POSITION_SETTLE_MS){
  /* Only save when no outstanding command can resume stepping on its own. */
  lock=irq_lock();
  int idle=axes[0].target==axes[0].position&&axes[1].target==axes[1].position
      &&!can_move(0u,axes[0].jog)&&!can_move(1u,axes[1].jog);
  if(idle)(void)save_position(1);
  irq_restore(lock);
 }
}
static void arm(void)
{
 if(g_adc_fault||!sample_valid||g_position_fault){return;}
 if(button_mask&15u){return;}
 g_boot_home_pending=0u;if(g_returning)return;
 if(!g_position_known)g_mode_auto=0u;
 stop_motion();g_enabled=1u;
}
static void select_mode(int automatic)
{
 if(g_returning)disarm();
 g_boot_home_pending=0u;stop_motion();g_mode_auto=(uint8_t)(automatic!=0&&g_position_known);
}
static void request_move(unsigned axis,int sign)
{
 if(!g_enabled||g_returning)return;
 if(g_mode_auto)select_mode(0);
 if(can_move(axis,sign)&&!prepare_motion())return;
 uint32_t lock=irq_lock();
 if(sign<0&&axes[axis].position<=-axes[axis].limit)ui_limit_attempts|=axis?UI_LIMIT_DOWN:UI_LIMIT_LEFT;
 if(sign>0&&axes[axis].position>=axes[axis].limit)ui_limit_attempts|=axis?UI_LIMIT_UP:UI_LIMIT_RIGHT;
 axis_move(&axes[axis],sign*MANUAL_TAP_STEPS);irq_restore(lock);
}
static void on_key(unsigned i,int down,uint32_t now)
{
 key_t *k=&keys[i];
 if(down){k->pressed=now;k->long_fired=0;k->held=0;
 }else if(i==4u && !k->long_fired){select_mode(!g_mode_auto);}
}
static void poll_keys(uint32_t now)
{
 uint8_t presses=0u;
 for(unsigned i=0;i<5u;++i){
  key_t *k=&keys[i];uint8_t raw=(uint8_t)!pin_read(gpio_a,key_pins[i]);
  if(raw!=k->candidate){k->candidate=raw;k->changed=now;}
  else if(raw!=k->stable && (uint32_t)(now-k->changed)>=KEY_DEBOUNCE_MS){k->stable=raw;on_key(i,raw,now);if(raw)presses|=(uint8_t)(1u<<i);}
 }
 button_mask=0;for(unsigned i=0;i<5u;++i)if(keys[i].stable)button_mask|=(uint8_t)(1u<<i);
 if(g_boot_home_pending&&button_mask)disarm();
 if(g_returning){if(presses){disarm();keys[4].long_fired=1u;}return;}
 static uint8_t chord,chord_fired;static uint32_t chord_at;
 uint8_t next=(button_mask&15u)==10u?1u:((button_mask&15u)==5u&&!g_enabled?2u:0u);
 if(next!=chord){chord=next;chord_at=now;chord_fired=0u;}
 if(chord){
  stop_motion();
  if(!chord_fired&&(uint32_t)(now-chord_at)>=(chord==1u?HOME_CHORD_MS:ZERO_CHORD_MS)){
   chord_fired=1u;if(chord==1u)(void)begin_home(now);else (void)set_zero();
  }
  return;
 }
 key_t *mode=&keys[4];
 if(mode->stable && !mode->long_fired && (uint32_t)(now-mode->pressed)>=MODE_LONG_PRESS_MS){mode->long_fired=1u;if(g_enabled)disarm();else arm();}
 if(!g_enabled||g_mode_auto)return;
 for(unsigned i=0;i<4u;++i)if(presses&(1u<<i)){
  unsigned a=(i==0u||i==2u)?0u:1u;uint8_t opposite=(uint8_t)(a?10u:5u);
  if((button_mask&opposite)!=opposite)request_move(a,(i==0u||i==1u)?-1:1);
 }
 int pan=0,tilt=0;
 if(keys[0].stable&&(uint32_t)(now-keys[0].pressed)>=MANUAL_HOLD_MS)pan=-1;
 if(keys[2].stable&&(uint32_t)(now-keys[2].pressed)>=MANUAL_HOLD_MS)pan=1;
 if(keys[1].stable&&(uint32_t)(now-keys[1].pressed)>=MANUAL_HOLD_MS)tilt=-1;
 if(keys[3].stable&&(uint32_t)(now-keys[3].pressed)>=MANUAL_HOLD_MS)tilt=1;
 if(keys[0].stable&&keys[2].stable)pan=0;
 if(keys[1].stable&&keys[3].stable)tilt=0;
 if((can_move(0u,pan)||can_move(1u,tilt))&&!prepare_motion())return;
 uint32_t lock=irq_lock();
 if(keys[0].stable&&keys[2].stable)axis_stop(&axes[0]);else if(pan||axes[0].jog)axis_jog(&axes[0],pan);
 if(keys[1].stable&&keys[3].stable)axis_stop(&axes[1]);else if(tilt||axes[1].jog)axis_jog(&axes[1],tilt);
 irq_restore(lock);
}
static void sample_sensors(void)
{
 for(unsigned ch=0;ch<5u;++ch){
  uint32_t sum=0;uint16_t value;
  for(unsigned n=0;n<8u;++n){if(!board_adc_read(ch,&value)){g_adc_fault=1u;disarm();return;}sum+=value;}
  uint16_t raw=(uint16_t)((sum+4u)/8u);
  if(sample_valid)raw=(uint16_t)(((uint32_t)g_adc_raw[ch]*3u+raw+2u)/4u);
  g_adc_raw[ch]=raw;
  if(ch<4u){int b=(int)light_from_adc(raw)+light_offset[ch];g_light[ch]=(uint16_t)(b<0?0:(b>1000?1000:b));}
  else g_battery_mv=battery_from_adc(raw);
 }
 sample_valid=1u;
 if(g_mode_auto&&g_enabled&&!g_returning&&(button_mask&15u)!=10u){
  pan_tracking=track_direction(g_light[SENSOR_RIGHT_ADC],g_light[SENSOR_LEFT_ADC],pan_tracking);
  tilt_tracking=track_direction(g_light[SENSOR_UP_ADC],g_light[SENSOR_DOWN_ADC],tilt_tracking);
  if((can_move(0u,pan_tracking)||can_move(1u,tilt_tracking))&&!prepare_motion())return;
  uint32_t lock=irq_lock();axis_jog(&axes[0],pan_tracking);axis_jog(&axes[1],tilt_tracking);irq_restore(lock);
 }
}
static void putstr(const char *s){while(*s)board_uart_put(*s++);}
static unsigned append(char *buf,unsigned n,const char *s)
{while(*s&&n<20u)buf[n++]=*s++;buf[n]=0;return n;}
static unsigned number(char *buf,unsigned n,int32_t v)
{
 char digits[11];unsigned k=0;uint32_t x;
 if(v<0){n=append(buf,n,"-");x=(uint32_t)(-(int64_t)v);}else x=(uint32_t)v;
 do{digits[k++]=(char)('0'+x%10u);x/=10u;}while(x&&k<sizeof(digits));
 while(k&&n<20u){buf[n++]=digits[--k];}
 buf[n]=0;return n;
}
static void status(void)
{
 char b[21];unsigned n;
 putstr("IFNE v1.1 ");putstr(g_mode_auto?"AUTO ":"MANUAL ");putstr(g_enabled?"ARMED\r\n":"SAFE\r\n");
 putstr(g_returning?"HOME=RETURNING\r\n":(g_position_known?"HOME=CALIBRATED\r\n":"HOME=SET_ZERO_REQUIRED\r\n"));
 for(unsigned i=0;i<5u;++i){n=append(b,0,"ADC");n=number(b,n,(int32_t)i);n=append(b,n,"=");number(b,n,g_adc_raw[i]);putstr(b);putstr(" ");}putstr("\r\n");
 n=append(b,0,"VBAT_MV=");number(b,n,g_battery_mv);putstr(b);putstr(" VLEVEL_EST=");number(b,0,voltage_percent(g_battery_mv));putstr(b);putstr("%\r\n");
 uint32_t lock=irq_lock();int32_t p=axes[0].position,t=axes[1].position;irq_restore(lock);
 n=append(b,0,"PAN=");n=number(b,n,p);n=append(b,n," TILT=");number(b,n,t);putstr(b);putstr("\r\n");
}
static int equal(const char *a,const char *b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static void command(const char *s)
{
 if(equal(s,"STATUS")){status();return;}
 if(equal(s,"STOP")||equal(s,"S")){disarm();putstr("SAFE\r\n");return;}
 if(equal(s,"ARM")){arm();status();return;}
 if(equal(s,"AUTO")){select_mode(1);status();return;}
 if(equal(s,"MANUAL")){select_mode(0);status();return;}
 if(equal(s,"LEFT")){request_move(0u,-1);return;}
 if(equal(s,"RIGHT")){request_move(0u,1);return;}
 if(equal(s,"UP")){request_move(1u,1);return;}
 if(equal(s,"DOWN")){request_move(1u,-1);return;}
 if(equal(s,"HOME")){putstr(begin_home(millis())?"Returning to saved zero; any key stops.\r\n":"HOME refused: calibrate ZERO first / check ADC.\r\n");return;}
 if(equal(s,"ZERO")&&!g_enabled){
  putstr(set_zero()?"Current pose saved as zero.\r\n":"Zero save failed.\r\n");return;
 }
 if((equal(s,"REV PAN")||equal(s,"REV TILT"))&&!g_enabled){
  unsigned i=equal(s,"REV PAN")?0u:1u;disarm();uint32_t lock=irq_lock();axes[i].phase_sign=(int8_t)-axes[i].phase_sign;irq_restore(lock);
  (void)save_position(0);g_position_known=0u;putstr("Direction changed; realign and ZERO to save a new reference.\r\n");return;
 }
 if(equal(s,"CAL")&&!g_enabled&&sample_valid){
  int mean=0;for(unsigned i=0;i<4u;++i)mean+=(int)light_from_adc(g_adc_raw[i]);mean/=4;
  if(mean<100){putstr("Use uniform brighter light for CAL.\r\n");return;}
  for(unsigned i=0;i<4u;++i){light_offset[i]=(int16_t)(mean-(int)light_from_adc(g_adc_raw[i]));}
  putstr("Equal-light offsets applied in RAM.\r\n");return;
 }
 putstr("Commands: STATUS MANUAL AUTO ARM STOP HOME LEFT RIGHT UP DOWN ZERO REV PAN REV TILT CAL\r\n");
}
static void poll_uart(void)
{
 static char line[32];static unsigned len;static uint8_t overflow;
 int ch;while((ch=board_uart_get())>=0){
  if(ch==13||ch==10){if(!overflow&&len){line[len]=0;command(line);}len=0;overflow=0;}
  else if(ch==8||ch==127){if(len)--len;}
  else if(ch>=32&&ch<=126){if(ch>='a'&&ch<='z')ch-=32;if(len<sizeof(line)-1u)line[len++]=(char)ch;else overflow=1u;}
 }
}
static void update_screen(void)
{
 ui_state_t state;
 state.light[0]=g_light[SENSOR_LEFT_ADC];state.light[1]=g_light[SENSOR_RIGHT_ADC];
 state.light[2]=g_light[SENSOR_UP_ADC];state.light[3]=g_light[SENSOR_DOWN_ADC];
 state.battery_mv=g_battery_mv;state.automatic=g_mode_auto;
 state.enabled=g_enabled;state.adc_fault=g_adc_fault;
 uint32_t lock=irq_lock();
 state.pan=axes[0].position;state.tilt=axes[1].position;
 state.pan_request=axes[0].jog?axes[0].jog:(int8_t)((axes[0].target>state.pan)-(axes[0].target<state.pan));
 state.tilt_request=axes[1].jog?axes[1].jog:(int8_t)((axes[1].target>state.tilt)-(axes[1].target<state.tilt));
 irq_restore(lock);
 /* Include a fresh key press before the hold-to-jog delay has elapsed. */
 if(g_enabled&&!g_mode_auto&&!g_returning){
  if(keys[0].stable||keys[2].stable)state.pan_request=(int8_t)((int)keys[2].stable-(int)keys[0].stable);
  if(keys[1].stable||keys[3].stable)state.tilt_request=(int8_t)((int)keys[3].stable-(int)keys[1].stable);
 }
 state.limit_attempts=ui_limit_attempts;ui_limit_attempts=0u;
 state.home_status=g_position_fault?UI_HOME_FAULT:(!g_position_known?UI_HOME_UNSET:(g_returning?UI_HOME_RETURN:(g_boot_home_pending?UI_HOME_WAIT:UI_HOME_READY)));
 uint32_t started=millis();ui_render(&state,started);
 g_ui_last_ms=millis()-started;if(g_ui_last_ms>g_ui_max_ms)g_ui_max_ms=g_ui_last_ms;
 ++g_ui_frames;
}
int main(void)
{
 axis_init(&axes[0],PAN_SOFT_LIMIT_STEPS,PAN_PHASE_SIGN);axis_init(&axes[1],TILT_SOFT_LIMIT_STEPS,TILT_PHASE_SIGN);
 board_init();
 restore_position();
 for(unsigned i=0;i<5u;++i){keys[i].candidate=(uint8_t)!pin_read(gpio_a,key_pins[i]);keys[i].stable=keys[i].candidate;keys[i].changed=millis();keys[i].pressed=millis();keys[i].long_fired=1u;}
 g_adc_fault=(uint8_t)!board_adc_init();display_init();
 if(!g_adc_fault){sample_sensors();}
 update_screen();board_watchdog_start();g_boot_marker=0x49464E45u;
 putstr("IFNE tracker v1.1 boot. Saved-position return only; unknown pose requires ZERO. HELP for commands.\r\n");
 uint32_t sampled=millis(),screened=millis();
 while(1){
  uint32_t now=millis();poll_keys(now);poll_uart();
  if((uint32_t)(now-sampled)>=100u&&!g_adc_fault){sampled=now;sample_sensors();}
  service_position(now);
  if((uint32_t)(now-screened)>=60u){screened=now;update_screen();}
  board_led(((now/(g_enabled?150u:650u))&1u)!=0u);board_watchdog_feed();
  uint32_t started=millis();ui_animate();g_ui_chunk_last_ms=millis()-started;
  if(g_ui_chunk_last_ms>g_ui_chunk_max_ms)g_ui_chunk_max_ms=g_ui_chunk_last_ms;
 }
}





