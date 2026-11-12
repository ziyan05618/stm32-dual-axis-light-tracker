#include "position_store.h"
#include <stdint.h>
static uint16_t flash_words[1024];
static int budget=-1;
static unsigned erase_count;
volatile uint32_t checks;
#define CHECK(e) do { ++checks;if(!(e))return __LINE__; } while(0)
uint16_t position_flash_read(uint32_t address){return flash_words[(address-POSITION_FLASH_BASE)/2u];}
int position_flash_program(uint32_t address,uint16_t value)
{
 if(address<POSITION_FLASH_BASE||address>=POSITION_FLASH_END||(address&1u))return 0;
 if(budget==0)return 0;
 if(budget>0)--budget;
 unsigned i=(address-POSITION_FLASH_BASE)/2u;
 if((flash_words[i]&value)!=value)return 0;
 flash_words[i]=value;return 1;
}
int position_flash_erase(uint32_t address)
{
 if(address<POSITION_FLASH_BASE||address>=POSITION_FLASH_END||(address&1023u))return 0;
 if(budget==0)return 0;
 if(budget>0)--budget;
 ++erase_count;
 for(unsigned i=0;i<512u;++i)flash_words[(address-POSITION_FLASH_BASE)/2u+i]=0xFFFFu;
 return 1;
}
static void reset_flash(void){for(unsigned i=0;i<1024u;++i)flash_words[i]=0xFFFFu;budget=-1;erase_count=0;}
int selftest(void)
{
 saved_position_t input={768,-384,7,3,-1,1},output;
 reset_flash();CHECK(position_store_load(&output)==POSITION_MISSING);
 CHECK(position_store_save(&input,1));CHECK(position_store_load(&output)==POSITION_CLEAN);
 CHECK(output.pan==768&&output.tilt==-384&&output.pan_phase==7&&output.tilt_phase==3);
 CHECK(output.pan_sign==-1&&output.tilt_sign==1);
 CHECK(position_store_save(&input,0));CHECK(position_store_load(&output)==POSITION_INTERRUPTED);
 /* Simulate loss of power after every half-word in an attempted clean commit. */
 for(int n=0;n<11;++n){
  reset_flash();CHECK(position_store_save(&input,0));budget=n;
  input.pan=500;CHECK(!position_store_save(&input,1));
  CHECK(position_store_load(&output)==POSITION_INTERRUPTED);
  CHECK(output.pan==768);input.pan=768;
 }
 /* Dirty must commit before movement; interrupted dirty append retains prior clean. */
 for(int n=0;n<11;++n){
  reset_flash();CHECK(position_store_save(&input,1));budget=n;
  CHECK(!position_store_save(&input,0));CHECK(position_store_load(&output)==POSITION_CLEAN);
 }
 reset_flash();
 for(int i=0;i<500;++i){
  input.pan=i-250;input.tilt=250-i;
  CHECK(position_store_save(&input,i&1));
  CHECK(position_store_load(&output)==((i&1)?POSITION_CLEAN:POSITION_INTERRUPTED));
  CHECK(output.pan==input.pan&&output.tilt==input.tilt);
 }
 CHECK(erase_count>8u);
 /* Rollover failure: the page containing the last valid entry survives. */
 for(int n=0;n<12;++n){
  reset_flash();
  for(unsigned i=0;i<42u;++i)CHECK(position_store_save(&input,0));
  budget=n;CHECK(!position_store_save(&input,1));
  CHECK(position_store_load(&output)==POSITION_INTERRUPTED);
 }
 reset_flash();input.pan=100;CHECK(position_store_save(&input,1));
 flash_words[4]^=1u;CHECK(position_store_load(&output)==POSITION_MISSING);
 reset_flash();input.pan=769;CHECK(!position_store_save(&input,1));
 CHECK(position_store_load(&output)==POSITION_MISSING);
 return 0;
}
