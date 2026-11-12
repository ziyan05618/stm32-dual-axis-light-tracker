/* Exercise the real main.c state machine, not a reimplementation of its logic. */
#define main tracker_entry
#include "../src/main.c"
#undef main

static uint16_t flash_words[1024];
static int flash_fail;
static int auto_contrast;
volatile uint32_t checks;
#define CHECK(e) do { ++checks;if(!(e))return __LINE__; } while(0)
volatile uint32_t g_millis;
volatile uint8_t g_io_ready;
void board_coils(uint8_t pan,uint8_t tilt){(void)pan;(void)tilt;}
int board_adc_read(unsigned channel,uint16_t *v){*v=channel==4u?2400u:(auto_contrast&&channel==0u?3500u:1000u);return 1;}
void board_uart_put(char c){(void)c;}
uint16_t position_flash_read(uint32_t at){return flash_words[(at-POSITION_FLASH_BASE)/2u];}
int position_flash_program(uint32_t at,uint16_t value)
{
 if(flash_fail)return 0;
 unsigned i=(at-POSITION_FLASH_BASE)/2u;
 if((flash_words[i]&value)!=value)return 0;
 flash_words[i]=value;return 1;
}
int position_flash_erase(uint32_t at)
{
 if(flash_fail)return 0;
 for(unsigned i=0;i<512u;++i)flash_words[(at-POSITION_FLASH_BASE)/2u+i]=0xFFFFu;
 return 1;
}
static void pin_keys(uint8_t mask)
{
 uint32_t idr=0xFFFFu;
 for(unsigned i=0;i<5u;++i)if(mask&(1u<<i))idr&=~(1u<<key_pins[i]);
 REG32(GPIOA_BASE+8u)=idr;
}
static void advance(unsigned ms)
{
 while(ms--){SysTick_Handler();poll_keys(millis());service_position(millis());}
}
static void reset_runtime(void)
{
 pin_keys(0u);advance(40u);
 g_enabled=0u;g_mode_auto=1u;g_adc_fault=0u;sample_valid=1u;button_mask=0u;
 g_position_known=0u;g_position_dirty=0u;g_position_fault=0u;g_returning=0u;g_boot_home_pending=0u;
 g_millis=0u;g_io_ready=1u;home_started=0u;boot_home_at=0u;quiet_at=0u;quiet_pan=0;quiet_tilt=0;
 for(unsigned i=0;i<5u;++i){keys[i].candidate=0u;keys[i].stable=0u;keys[i].long_fired=0u;keys[i].held=0u;keys[i].changed=0u;keys[i].pressed=0u;}
 axis_init(&axes[0],PAN_SOFT_LIMIT_STEPS,1);axis_init(&axes[1],TILT_SOFT_LIMIT_STEPS,1);
}
int selftest(void)
{
 for(unsigned i=0;i<1024u;++i)flash_words[i]=0xFFFFu;
 reset_runtime();restore_position();CHECK(!g_position_known&&!g_boot_home_pending);
 CHECK(!begin_home(0u));arm();CHECK(g_enabled&&!g_mode_auto);disarm();
 /* Board-only calibration gesture: SAFE + left/right held for 2.5 seconds. */
 pin_keys(5u);advance(2600u);CHECK(g_position_known&&!g_enabled&&!g_position_dirty);
 pin_keys(0u);advance(40u);
 g_mode_auto=0u;arm();request_move(0u,1);
 CHECK(g_position_dirty&&g_enabled);saved_position_t saved;
 CHECK(position_store_load(&saved)==POSITION_INTERRUPTED);
 advance(1200u);CHECK(axes[0].position==MANUAL_TAP_STEPS&&!g_position_dirty);
 CHECK(position_store_load(&saved)==POSITION_CLEAN&&saved.pan==MANUAL_TAP_STEPS);
 reset_runtime();restore_position();CHECK(g_position_known&&g_boot_home_pending&&axes[0].position==16);
 advance(1400u);CHECK(!g_enabled&&axes[0].position==16);
 advance(600u);CHECK(!g_returning&&!g_enabled&&axes[0].position==0&&!g_position_dirty);
 CHECK(position_store_load(&saved)==POSITION_CLEAN&&saved.pan==0);
 /* Simultaneous opposite keys must not add a stray single-key tap. */
 g_mode_auto=0u;arm();pin_keys(5u);advance(100u);CHECK(axes[0].position==0);
 pin_keys(0u);advance(40u);disarm();
 /* Return from maximum software travel, then remain SAFE without auto resuming. */
 axes[0].position=768;axes[1].position=-384;stop_motion();CHECK(save_position(1));
 reset_runtime();restore_position();advance(9000u);
 CHECK(axes[0].position==0&&axes[1].position==0&&!g_enabled&&!g_returning);
 advance(1000u);CHECK(!g_enabled&&axes[0].position==0);
 /* A runtime S2+S4 chord starts HOME; a fresh direction key cancels it. */
 axes[0].position=300;stop_motion();CHECK(save_position(1));
 g_mode_auto=1u;arm();auto_contrast=1;
 pin_keys(10u);advance(100u);sample_sensors();CHECK(axes[0].jog==0&&axes[0].position==300);
 advance(1500u);CHECK(g_returning&&g_enabled&&axes[0].position<300);
 pin_keys(11u);advance(40u);CHECK(!g_returning&&!g_enabled);
 int32_t cancelled=axes[0].position;pin_keys(0u);advance(800u);CHECK(axes[0].position==cancelled);
 /* Any held key cancels the boot delay, without silently rearming. */
 reset_runtime();restore_position();CHECK(g_boot_home_pending);
 pin_keys(1u);advance(2000u);CHECK(!g_boot_home_pending&&!g_enabled&&axes[0].position==cancelled);
 pin_keys(0u);advance(40u);
 /* Power loss mid-motion leaves a dirty record and disables autonomous motion. */
 g_mode_auto=0u;arm();request_move(0u,-1);advance(30u);CHECK(g_position_dirty);
 reset_runtime();restore_position();CHECK(!g_position_known&&!g_boot_home_pending);
 select_mode(1);CHECK(!g_mode_auto);advance(2000u);CHECK(!g_enabled&&axes[0].position==0);
 /* Calibration repairs an interrupted record; I/O errors prevent movement. */
 CHECK(set_zero());flash_fail=1;g_mode_auto=0u;arm();request_move(0u,1);advance(500u);
 CHECK(g_position_fault&&!g_position_known&&!g_enabled&&axes[0].position==0);
 arm();CHECK(!g_enabled);flash_fail=0;CHECK(set_zero()&&!g_position_fault);
 /* HOME handles the wrapping millisecond clock and a STOP command. */
 axes[0].position=-768;stop_motion();CHECK(save_position(1));g_millis=0xFFFFFF00u;
 CHECK(begin_home(millis()));advance(1000u);CHECK(g_returning&&axes[0].position>-768);
 command("STOP");CHECK(!g_returning&&!g_enabled);cancelled=axes[0].position;
 advance(1000u);CHECK(axes[0].position==cancelled&&!g_position_dirty);
 CHECK(begin_home(millis()));g_millis+=HOME_TIMEOUT_MS;service_position(millis());
 CHECK(!g_returning&&!g_enabled&&!g_position_known);
 CHECK(position_store_load(&saved)==POSITION_INTERRUPTED);
 return 0;
}
