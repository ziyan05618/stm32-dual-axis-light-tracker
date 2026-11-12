#include "display.h"
#include "control.h"
#include "ui.h"
#include "tracker_config.h"

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
#define PANEL RGB(32u,28u,48u)
#define WHITE RGB(238u,233u,243u)
#define MUTED RGB(155u,149u,177u)
#define GOLD RGB(227u,199u,140u)
#define MINT RGB(180u,217u,184u)

static ui_state_t previous;
static uint8_t ready;
static uint8_t character_ready;
volatile portrait_frame_t g_ui_portrait;
volatile uint8_t g_ui_anim_active,g_ui_anim_pending;
static portrait_frame_t next_frame,current_frame;
static unsigned animation_row;

typedef struct { int32_t position,velocity; } spring_t;
static spring_t pose[4];
static uint32_t pose_at;

static int8_t ease_pose(unsigned index,int target,unsigned ticks)
{
    spring_t *s=&pose[index];
    for(unsigned i=0;i<ticks;++i){
        int32_t error=target*16-s->position;
        s->velocity+=error/(index==3u?12:6)-s->velocity/(index==3u?3:2);
        s->position+=s->velocity;
        if(error>-12&&error<12&&s->velocity>-4&&s->velocity<4){s->position=target*16;s->velocity=0;}
    }
    int32_t value=s->position/16;
    if(value>96)value=96;
    if(value< -96)value=-96;
    return (int8_t)value;
}

static uint8_t position_limits(const ui_state_t *state)
{
    uint8_t mask=0u;
    if(state->pan<=-PAN_SOFT_LIMIT_STEPS)mask|=UI_LIMIT_LEFT;
    if(state->pan>=PAN_SOFT_LIMIT_STEPS)mask|=UI_LIMIT_RIGHT;
    if(state->tilt>=TILT_SOFT_LIMIT_STEPS)mask|=UI_LIMIT_UP;
    if(state->tilt<=-TILT_SOFT_LIMIT_STEPS)mask|=UI_LIMIT_DOWN;
    return mask;
}

void ui_animate(void)
{
    if(!g_ui_anim_active){
        if(!g_ui_anim_pending)return;
        current_frame=next_frame;g_ui_anim_pending=0u;animation_row=0u;g_ui_anim_active=1u;
    }
    display_character_rows(&current_frame,animation_row,6u);
    animation_row+=6u;
    if(animation_row>=58u)g_ui_anim_active=0u;
}

static void character(const ui_state_t *state,uint32_t now)
{
    static reaction_t event;
    static uint32_t event_at,event_duration,pan_at,tilt_at,settled_at,limit_at;
    static int8_t pan_dir,tilt_dir;
    static uint8_t battery_band,dark,bright,last_limits,last_blocked,limit_hint;
    static uint8_t face_eyes,face_mix;
    static const int8_t wave[32]={0,3,6,9,11,13,15,16,16,16,15,13,11,9,6,3,0,-3,-6,-9,-11,-13,-15,-16,-16,-16,-15,-13,-11,-9,-6,-3};
    unsigned phase=(now/120u)&255u;
    unsigned maximum=0u,sum=0u;
    for(unsigned i=0;i<4u;++i){if(state->light[i]>maximum)maximum=state->light[i];sum+=state->light[i];}
    uint8_t old_band=battery_band;
    if(state->battery_mv<1000u)battery_band=3u;
    else if(state->battery_mv<=3150u||(battery_band==2u&&state->battery_mv<3250u))battery_band=2u;
    else if(state->battery_mv<=3400u||((battery_band==1u||battery_band==2u)&&state->battery_mv<3500u))battery_band=1u;
    else battery_band=0u;
    if(maximum<20u)dark=1u;else if(maximum>=45u)dark=0u;
    if(sum>=3600u)bright=1u;else if(sum<3280u)bright=0u;
    if(!character_ready){event=REACTION_HELLO;event_at=now;event_duration=1800u;settled_at=now;pose_at=now;character_ready=1u;}
    else{
        if(state->enabled!=previous.enabled){event=state->enabled?REACTION_START:REACTION_STOP;event_at=now;event_duration=1200u;}
        else if(state->automatic!=previous.automatic){event=state->automatic?REACTION_AUTO:REACTION_MANUAL;event_at=now;event_duration=1200u;}
        else if((old_band==1u||old_band==2u)&&battery_band==0u){event=REACTION_RECOVERED;event_at=now;event_duration=1800u;}
        if(state->enabled&&state->pan!=previous.pan){
            pan_dir=(int8_t)((state->pan>previous.pan)-(state->pan<previous.pan));
            pan_at=now;
        }
        if(state->enabled&&state->tilt!=previous.tilt){
            tilt_dir=(int8_t)((state->tilt>previous.tilt)-(state->tilt<previous.tilt));
            tilt_at=now;
        }
    }
    uint8_t limits=position_limits(state),blocked=0u;
    if(state->enabled){
        if(state->pan_request<0)blocked|=limits&UI_LIMIT_LEFT;
        if(state->pan_request>0)blocked|=limits&UI_LIMIT_RIGHT;
        if(state->tilt_request>0)blocked|=limits&UI_LIMIT_UP;
        if(state->tilt_request<0)blocked|=limits&UI_LIMIT_DOWN;
    }
    uint8_t arrived=state->enabled?(uint8_t)(limits&~last_limits):0u;
    uint8_t attempted=state->enabled?(uint8_t)((blocked&~last_blocked)|(state->limit_attempts&limits)):0u;
    uint8_t trigger=(uint8_t)(arrived|attempted);
    if(!state->enabled||!(limits&limit_hint)||(uint32_t)(now-limit_at)>=1050u)limit_hint=0u;
    if(trigger&&(!limit_hint||!(trigger&limit_hint))){
        limit_hint=(uint8_t)(trigger&(uint8_t)(0u-trigger));
        limit_at=now;
    }
    last_limits=limits;last_blocked=blocked;
    /* Stop the blocked axis's follow-through without hiding motion on the other axis. */
    if(!state->enabled||(uint32_t)(now-pan_at)>=240u
       ||(pan_dir<0&&(limits&UI_LIMIT_LEFT))||(pan_dir>0&&(limits&UI_LIMIT_RIGHT)))pan_dir=0;
    if(!state->enabled||(uint32_t)(now-tilt_at)>=240u
       ||(tilt_dir>0&&(limits&UI_LIMIT_UP))||(tilt_dir<0&&(limits&UI_LIMIT_DOWN)))tilt_dir=0;
    uint8_t moving=(uint8_t)(pan_dir!=0||tilt_dir!=0);
    if(moving)limit_hint=0u; /* A fresh motion consumes an older boundary notification. */
    int horizontal=(int)state->light[0]-(int)state->light[1];
    int vertical=(int)state->light[2]-(int)state->light[3];
    int aligned=state->enabled&&state->automatic&&!dark&&!moving&&!blocked
        &&horizontal>=-20&&horizontal<=20&&vertical>=-20&&vertical<=20;
    if(!aligned)settled_at=now;
    reaction_t reaction=REACTION_IDLE;
    if(state->adc_fault)reaction=REACTION_FAULT;
    else if(battery_band==2u)reaction=REACTION_EMPTY;
    else if(moving){
        if(pan_dir&&tilt_dir)reaction=REACTION_DIAGONAL;
        else if(pan_dir)reaction=pan_dir<0?REACTION_LEFT:REACTION_RIGHT;
        else reaction=tilt_dir>0?REACTION_UP:REACTION_DOWN;
    }
    else if(limit_hint){
        if(limit_hint&UI_LIMIT_LEFT)reaction=REACTION_LIMIT_LEFT;
        else if(limit_hint&UI_LIMIT_RIGHT)reaction=REACTION_LIMIT_RIGHT;
        else if(limit_hint&UI_LIMIT_UP)reaction=REACTION_LIMIT_UP;
        else reaction=REACTION_LIMIT_DOWN;
    }
    else if((uint32_t)(now-event_at)<event_duration)reaction=event;
    else if(battery_band==1u)reaction=REACTION_LOW;
    else if(battery_band==3u)reaction=REACTION_UNKNOWN;
    else if(dark)reaction=REACTION_DARK;
    else if(bright)reaction=REACTION_BRIGHT;
    else if(aligned&&(uint32_t)(now-settled_at)>=1200u)reaction=REACTION_ALIGNED;
    int dx=0,dy=wave[(now/90u)&31u]/2,lean=0,sway=wave[(now/120u+8u)&31u]/4;
    uint8_t eyes=EYES_OPEN,icon=ICON_NONE;
    unsigned age=(uint32_t)(now-event_at);
    if(moving&&!state->adc_fault){
        dx=pan_dir*(20+wave[(now/60u)&31u]/5);lean=pan_dir*12;
        dy-=tilt_dir*26;sway-=pan_dir*12;
    }
    switch(reaction){
    case REACTION_HELLO:case REACTION_START:case REACTION_RECOVERED:
        eyes=EYES_HAPPY;dy-=age<180u?26:(age<420u?-5:0);icon=ICON_HEART;break;
    case REACTION_STOP:dy+=age<240u?24:10;eyes=age<480u?EYES_CLOSED:EYES_TIRED;icon=ICON_MOON;break;
    case REACTION_MANUAL:case REACTION_AUTO:eyes=EYES_HAPPY;lean=wave[(age/40u)&31u]/2;icon=ICON_STAR;break;
    case REACTION_LEFT:icon=ICON_LEFT;break;
    case REACTION_RIGHT:icon=ICON_RIGHT;break;
    case REACTION_UP:icon=ICON_UP;break;
    case REACTION_DOWN:eyes=EYES_HAPPY;icon=ICON_DOWN;break;
    case REACTION_DIAGONAL:icon=ICON_STAR;break;
    case REACTION_DARK:dy+=18;eyes=(now/600u)%8u<6u?EYES_CLOSED:EYES_TIRED;icon=ICON_MOON;break;
    case REACTION_BRIGHT:eyes=EYES_HAPPY;icon=ICON_SUN;break;
    case REACTION_ALIGNED:eyes=EYES_HAPPY;icon=ICON_HEART;break;
    case REACTION_LOW:dy+=20;lean=7;eyes=EYES_TIRED;icon=ICON_BATTERY;break;
    case REACTION_EMPTY:dy+=30;lean=9;eyes=(now/900u)&1u?EYES_TIRED:EYES_CLOSED;icon=ICON_BATTERY;break;
    case REACTION_LIMIT_LEFT:case REACTION_LIMIT_RIGHT:
    case REACTION_LIMIT_UP:case REACTION_LIMIT_DOWN:
        age=(uint32_t)(now-limit_at);
        dx=(age<600u)?wave[(age/30u+8u)&31u]/2:0;eyes=EYES_TIRED;icon=ICON_ALERT;break;
    case REACTION_FAULT:lean=wave[(now/100u)&31u]/3;eyes=EYES_TIRED;icon=ICON_ALERT;break;
    case REACTION_UNKNOWN:icon=ICON_BATTERY;break;
    default:break;
    }
    unsigned elapsed=(uint32_t)(now-pose_at),ticks=elapsed/30u;
    if(ticks>8u){ticks=8u;pose_at=now;}else pose_at+=ticks*30u;
    portrait_frame_t frame={0};
    frame.dx=ease_pose(0u,dx,ticks);frame.dy=ease_pose(1u,dy,ticks);
    frame.lean=ease_pose(2u,lean,ticks);frame.sway=ease_pose(3u,sway,ticks);
    if(face_eyes!=eyes){if(face_mix>3u)face_mix-=4u;else{face_mix=0u;face_eyes=eyes;}}
    else if(face_mix<15u){face_mix=(uint8_t)(face_mix>11u?15u:face_mix+4u);}
    frame.eyes=face_eyes;frame.eye_mix=face_mix;
    unsigned blink=now%4800u;
    if(eyes==EYES_OPEN&&blink>=4440u&&blink<4740u){
        static const uint8_t eyelid[5]={4u,10u,15u,10u,4u};
        frame.eyes=EYES_CLOSED;frame.eye_mix=eyelid[(blink-4440u)/60u];
    }
    frame.reaction=(uint8_t)reaction;frame.icon=icon;frame.phase=(uint8_t)phase;
    g_ui_portrait=frame;next_frame=frame;g_ui_anim_pending=1u;
}

static unsigned decimal(char *out, int32_t value, uint8_t sign)
{
    char reversed[10]; unsigned count=0u, n=0u;
    uint32_t v;
    if(value<0){out[n++]='-';v=(uint32_t)(-(int64_t)value);}
    else{if(sign)out[n++]='+';v=(uint32_t)value;}
    do{reversed[count++]=(char)('0'+v%10u);v/=10u;}while(v&&count<10u);
    while(count){out[n++]=reversed[--count];}
    out[n]=0;return n;
}
static void badge(unsigned x, const char *text, uint16_t bg, uint16_t fg)
{
    display_rect(x+2u,3u,24u,1u,bg);
    display_rect(x+1u,4u,26u,13u,bg);
    display_rect(x+2u,17u,24u,1u,bg);
    display_text(x+2u,4u,text,fg,bg);
}
static void sensor(unsigned index, uint16_t value)
{
    char buffer[12]; const unsigned y=24u+18u*index;
    if(value>1000u)value=1000u;
    unsigned n=decimal(buffer,value,0u);
    display_rect(90u,y+2u,30u,10u,PANEL);
    display_text(120u-n*6u,y+3u,buffer,WHITE,PANEL);
    display_rect(83u,y+12u,37u,2u,RGB(57u,48u,70u));
    unsigned w=(37u*value+500u)/1000u;
    if(w)display_rect(83u,y+12u,w,2u,index<2u?MINT:GOLD);
}
static void position(unsigned x,int32_t value,int32_t limit)
{
    char buffer[12];
    uint8_t at_limit=(uint8_t)(value>=limit||value<=-limit);
    /* The configured travel caps fit four signed characters. */
    if(value>999)value=999;
    if(value< -999)value=-999;
    unsigned n=decimal(buffer,value,1u);
    display_rect(x,100u,30u,9u,PANEL);
    display_text(x+24u-n*6u,100u,buffer,at_limit?GOLD:WHITE,PANEL);
    if(at_limit)display_rect(x+27u,101u,2u,6u,GOLD);
}
static void battery(uint16_t mv)
{
    char buffer[12]; uint8_t percent=voltage_percent(mv);
    uint16_t color=mv<3300u?RGB(239u,144u,151u):MINT;
    display_rect(33u,113u,86u,10u,PANEL);
    if(mv<1000u){display_text(35u,114u,"--.--V",MUTED,PANEL);}
    else{
        unsigned centivolts=(mv+5u)/10u;
        buffer[0]=(char)('0'+centivolts/100u);buffer[1]='.';
        buffer[2]=(char)('0'+centivolts/10u%10u);buffer[3]=(char)('0'+centivolts%10u);
        buffer[4]='V';buffer[5]=0;
        display_text(35u,114u,buffer,WHITE,PANEL);
    }
    display_text(72u,112u,"估",MUTED,PANEL);
    if(mv<1000u){display_text(100u,114u,"--",MUTED,PANEL);}
    else{unsigned n=decimal(buffer,percent,0u);buffer[n++]='%';buffer[n]=0;display_text(119u-n*6u,114u,buffer,color,PANEL);}
}

static void effects(uint32_t now)
{
    static const uint16_t breathe[8]={RGB(104u,87u,104u),RGB(132u,108u,115u),RGB(172u,145u,133u),GOLD,GOLD,RGB(172u,145u,133u),RGB(132u,108u,115u),RGB(104u,87u,104u)};
    uint16_t glow=breathe[(now/240u)%8u];
    display_restore(54u,8u,7u,7u);
    display_rect(57u,8u,1u,7u,glow);display_rect(54u,11u,7u,1u,glow);
    display_rect(56u,10u,3u,3u,glow);
}

void ui_render(const ui_state_t *state,uint32_t now)
{
    if(!ready)display_restore(0u,0u,128u,128u);
    if(!ready||state->automatic!=previous.automatic)badge(62u,state->automatic?"自动":"手动",RGB(57u,49u,76u),GOLD);
    if(!ready||state->enabled!=previous.enabled||state->adc_fault!=previous.adc_fault||state->home_status!=previous.home_status){
        if(state->adc_fault)badge(94u,"故障",RGB(87u,40u,59u),RGB(255u,186u,191u));
        else if(state->home_status==UI_HOME_FAULT)badge(94u,"存错",RGB(87u,40u,59u),RGB(255u,186u,191u));
        else if(state->home_status==UI_HOME_RETURN)badge(94u,"回中",RGB(41u,66u,59u),MINT);
        else if(state->home_status==UI_HOME_WAIT)badge(94u,"准备",RGB(51u,45u,65u),GOLD);
        else if(state->enabled)badge(94u,"运行",RGB(41u,66u,59u),MINT);
        else if(state->home_status==UI_HOME_UNSET)badge(94u,"待校",RGB(51u,45u,65u),GOLD);
        else badge(94u,"安全",RGB(51u,45u,65u),RGB(188u,179u,207u));
    }
    for(unsigned i=0;i<4u;++i){if(!ready||state->light[i]!=previous.light[i])sensor(i,state->light[i]);}
    if(!ready||state->pan!=previous.pan)position(31u,state->pan,PAN_SOFT_LIMIT_STEPS);
    if(!ready||state->tilt!=previous.tilt)position(93u,state->tilt,TILT_SOFT_LIMIT_STEPS);
    if(!ready||state->battery_mv!=previous.battery_mv)battery(state->battery_mv);
    character(state,now);
    effects(now);
    previous=*state;ready=1u;
}
