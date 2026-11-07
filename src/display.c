#include "board.h"
#include "display.h"
#include "font5x7.h"
#include "font_zh12.h"
#include "ui_assets.h"

#define SCREEN_W 128u
#define SCREEN_H 128u

static void lcd_write8(uint8_t value,uint8_t data)
{
    if(data)pin_high(gpio_b,LCD_DC_PIN);else pin_low(gpio_b,LCD_DC_PIN);
    pin_low(gpio_b,LCD_SCK_PIN);
    for(unsigned i=0;i<8u;++i){if(value&0x80u)pin_high(gpio_b,LCD_MOSI_PIN);else pin_low(gpio_b,LCD_MOSI_PIN);pin_high(gpio_b,LCD_SCK_PIN);pin_low(gpio_b,LCD_SCK_PIN);value<<=1;}
}
static void lcd_command(uint8_t c){pin_low(gpio_b,LCD_CS_PIN);lcd_write8(c,0u);}
static void lcd_data8(uint8_t d){lcd_write8(d,1u);}
static void lcd_end(void){pin_high(gpio_b,LCD_CS_PIN);}
static void lcd_cmd_data(uint8_t c,const uint8_t *data,unsigned n){lcd_command(c);while(n--)lcd_data8(*data++);lcd_end();}
static void lcd_u16(uint16_t v){lcd_data8((uint8_t)(v>>8));lcd_data8((uint8_t)v);}
static void lcd_window(unsigned x0,unsigned y0,unsigned x1,unsigned y1)
{
    lcd_command(0x2Au);lcd_u16((uint16_t)(x0+LCD_X_OFFSET));lcd_u16((uint16_t)(x1+LCD_X_OFFSET));lcd_end();
    lcd_command(0x2Bu);lcd_u16((uint16_t)(y0+LCD_Y_OFFSET));lcd_u16((uint16_t)(y1+LCD_Y_OFFSET));lcd_end();
    lcd_command(0x2Cu);
}
void display_rect(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t color)
{
    if(!w||!h||x>=SCREEN_W||y>=SCREEN_H)return;
    if(w>SCREEN_W-x){w=SCREEN_W-x;}
    if(h>SCREEN_H-y){h=SCREEN_H-y;}
    lcd_window(x,y,x+w-1u,y+h-1u);pin_high(gpio_b,LCD_DC_PIN);
    for(unsigned i=0;i<w*h;++i)lcd_u16(color);
    lcd_end();
}
void display_clear(uint16_t color){display_rect(0u,0u,SCREEN_W,SCREEN_H,color);}
void display_restore(unsigned x,unsigned y,unsigned w,unsigned h)
{
    if(!w||!h||x>=SCREEN_W||y>=SCREEN_H)return;
    if(w>SCREEN_W-x)w=SCREEN_W-x;
    if(h>SCREEN_H-y)h=SCREEN_H-y;
    lcd_window(x,y,x+w-1u,y+h-1u);pin_high(gpio_b,LCD_DC_PIN);
    for(unsigned row=0;row<h;++row)for(unsigned col=0;col<w;++col)lcd_u16(ui_background[(y+row)*SCREEN_W+x+col]);
    lcd_end();
}
static uint16_t blend(uint16_t foreground,uint16_t background,unsigned alpha)
{
    if(!alpha)return background;
    if(alpha>=15u)return foreground;
    unsigned inverse=15u-alpha;
    unsigned r=(((foreground>>11)*alpha+(background>>11)*inverse)+7u)/15u;
    unsigned g=(((((foreground>>5)&63u)*alpha+((background>>5)&63u)*inverse)+7u)/15u);
    unsigned b=(((foreground&31u)*alpha+(background&31u)*inverse)+7u)/15u;
    return (uint16_t)((r<<11)|(g<<5)|b);
}
static int floor_pixel(int value)
{
    return value>=0?value/16:-((-value+15)/16);
}
static uint16_t portrait_pixel(const portrait_frame_t *frame,int sx,int sy,uint16_t background)
{
    if(sx<0||sx>=PORTRAIT_W||sy<0||sy>=PORTRAIT_H)return background;
    unsigned index=(unsigned)sy*PORTRAIT_W+(unsigned)sx;
    unsigned alpha=(portrait_alpha[index/2u]>>((index&1u)*4u))&15u;
    if(!alpha)return background;
    uint16_t pixel=portrait_colors[index];
    if(frame->eyes>=EYES_CLOSED&&frame->eyes<=EYES_TIRED
       &&sx>=EXPRESSION_X&&sx<EXPRESSION_X+EXPRESSION_W
       &&sy>=EXPRESSION_Y&&sy<EXPRESSION_Y+EXPRESSION_H){
        unsigned expression=(frame->eyes-1u)*EXPRESSION_W*EXPRESSION_H;
        uint16_t expression_pixel=portrait_expressions[expression+(unsigned)(sy-EXPRESSION_Y)*EXPRESSION_W+(unsigned)(sx-EXPRESSION_X)];
        pixel=blend(expression_pixel,pixel,frame->eye_mix);
    }
    return blend(pixel,background,alpha);
}
static uint16_t interpolate(uint16_t a,uint16_t b,unsigned fraction)
{
    unsigned inverse=16u-fraction;
    unsigned r=((a>>11)*inverse+(b>>11)*fraction+8u)>>4;
    unsigned g=(((a>>5)&63u)*inverse+((b>>5)&63u)*fraction+8u)>>4;
    unsigned blue=((a&31u)*inverse+(b&31u)*fraction+8u)>>4;
    return (uint16_t)((r<<11)|(g<<5)|blue);
}
void display_character_rows(const portrait_frame_t *frame,unsigned first,unsigned count)
{
    /* Compose a short strip so the main loop can service input between strips. */
    static const uint8_t icons[][7]={
        {0,0,0,0,0,0,0}, {0x36,0x7f,0x7f,0x3e,0x1c,8,0},
        {8,0x18,0x38,0x7f,0x38,0x18,8}, {8,0x0c,0x0e,0x7f,0x0e,0x0c,8},
        {8,0x1c,0x3e,0x7f,8,8,8}, {8,8,8,0x7f,0x3e,0x1c,8},
        {0x1c,0x38,0x70,0x70,0x70,0x3a,0x1c}, {8,0x2a,0x1c,0x7f,0x1c,0x2a,8},
        {0x1c,0x7f,0x41,0x49,0x41,0x7f,0}, {8,0x1c,0x3e,0x77,0x77,0x7f,0x77},
        {8,8,0x1c,0x7f,0x1c,8,8}
    };
    unsigned reaction=frame->reaction<REACTION_COUNT?frame->reaction:REACTION_IDLE;
    uint16_t accent=0xE631u; /* pale gold */
    if(reaction==REACTION_LOW||reaction==REACTION_EMPTY||reaction==REACTION_FAULT)
        accent=(frame->phase&4u)?0xFCB4u:0xB36Fu;
    else if(reaction==REACTION_ALIGNED||reaction==REACTION_RECOVERED)accent=0xB6D7u;
    else if(reaction==REACTION_UNKNOWN)accent=0x9CB6u;
    if(first>=58u||!count)return;
    if(count>58u-first)count=58u-first;
    lcd_window(0u,22u+first,60u,21u+first+count);pin_high(gpio_b,LCD_DC_PIN);
    for(unsigned y=22u+first;y<22u+first+count;++y){
        int sy_sub=((int)y-22)*16-frame->dy;
        int sy=floor_pixel(sy_sub);
        unsigned fy=(unsigned)(sy_sub-sy*16);
        int depth=sy<0?0:(sy>=PORTRAIT_H?PORTRAIT_H-1:sy);
        int bend=frame->lean*(PORTRAIT_H-1-depth)/58+frame->sway*depth/58;
        for(unsigned x=0u;x<61u;++x){
            uint16_t color=ui_background[y*SCREEN_W+x];
            int sx_sub=((int)x-1)*16-frame->dx-bend;
            int sx=floor_pixel(sx_sub);
            unsigned fx=(unsigned)(sx_sub-sx*16);
            if(sx>=-1&&sx<PORTRAIT_W&&sy>=-1&&sy<PORTRAIT_H){
                uint16_t a=portrait_pixel(frame,sx,sy,color);
                if(fx)a=interpolate(a,portrait_pixel(frame,sx+1,sy,color),fx);
                if(fy){
                    uint16_t b=portrait_pixel(frame,sx,sy+1,color);
                    if(fx)b=interpolate(b,portrait_pixel(frame,sx+1,sy+1,color),fx);
                    a=interpolate(a,b,fy);
                }
                color=a;
            }
            /* Petals belong to the composite, so restoring them cannot erase hair. */
            for(unsigned i=0u;i<3u;++i){
                unsigned tick=frame->phase+i*19u;
                unsigned px=i==1u?50u+(tick/9u)%4u:3u+(tick/7u+i*2u)%6u;
                unsigned py=25u+tick%39u;
                if((y==py&&x>=px+1u&&x<px+3u)||(y==py+1u&&x>=px&&x<px+3u))color=0xD597u;
                else if(y==py+2u&&x>=px&&x<px+2u)color=0x9371u;
            }
            if(y>=67u&&y<80u&&x>=4u&&x<58u){
                color=(y==67u||y==79u||x==4u||x==57u)?0x41E9u:0x18A5u;
                if(y>=68u&&y<79u&&x>=7u&&x<55u){
                    unsigned index=reaction*(REACTION_TEXT_W*REACTION_TEXT_H/4u)+(y-68u)*12u+(x-7u)/4u;
                    unsigned alpha=(reaction_text[index]>>(((x-7u)&3u)*2u))&3u;
                    color=blend(accent,color,alpha*5u);
                }
            }
            unsigned iy=26u+((frame->icon==ICON_HEART&&(frame->phase&2u))?1u:0u);
            if(frame->icon<=ICON_STAR&&x>=49u&&x<56u&&y>=iy&&y<iy+7u
               &&(icons[frame->icon][y-iy]&(1u<<(55u-x))))color=accent;
            lcd_u16(color);
        }
    }
    lcd_end();
}
static uint8_t font_index(char c){if(c<32||c>126)return (uint8_t)'?'-32u;return (uint8_t)c-32u;}
static void ascii_glyph(unsigned x,unsigned y,char c,uint16_t fg,uint16_t bg)
{
    const uint8_t *glyph=font5x7[font_index(c)];
    lcd_window(x,y,x+5u,y+7u);pin_high(gpio_b,LCD_DC_PIN);
    for(unsigned row=0;row<8u;++row)for(unsigned col=0;col<6u;++col)lcd_u16((col<5u&&(glyph[col]&(1u<<row)))?fg:bg);
    lcd_end();
}
static uint16_t decode_utf8(const unsigned char **p)
{
    uint16_t code;
    if((**p&0xF0u)==0xE0u){code=(uint16_t)((((*p)[0]&0x0Fu)<<12)|(((*p)[1]&0x3Fu)<<6)|((*p)[2]&0x3Fu));*p+=3;return code;}
    if((**p&0xE0u)==0xC0u){code=(uint16_t)((((*p)[0]&0x1Fu)<<6)|((*p)[1]&0x3Fu));*p+=2;return code;}
    ++*p;return 0x003Fu;
}
static void chinese_glyph(unsigned x,unsigned y,uint16_t code,uint16_t fg,uint16_t bg)
{
    unsigned index=0;while(index<ZH_COUNT&&font_zh_codepoints[index]!=code)++index;
    lcd_window(x,y,x+11u,y+11u);pin_high(gpio_b,LCD_DC_PIN);
    for(unsigned row=0;row<12u;++row)for(unsigned col=0;col<12u;++col){uint16_t bits=index<ZH_COUNT?font_zh12[index][row]:0x7FFu;lcd_u16((bits&(1u<<(11u-col)))?fg:bg);}
    lcd_end();
}
void display_text(unsigned x,unsigned y,const char *text,uint16_t fg,uint16_t bg)
{
    const unsigned char *p=(const unsigned char *)text;
    if(y>=SCREEN_H)return;
    while(*p&&x<SCREEN_W){
        if(*p<0x80u){if(x+6u>SCREEN_W||y+8u>SCREEN_H)break;ascii_glyph(x,y,(char)*p,fg,bg);++p;x+=6u;}
        else{if(x+12u>SCREEN_W||y+12u>SCREEN_H)break;chinese_glyph(x,y,decode_utf8(&p),fg,bg);x+=12u;}
    }
}
void display_meter(unsigned x,unsigned y,unsigned width,unsigned percent,uint32_t phase)
{
    if(percent>100u)percent=100u;
    display_rect(x,y,width,4u,0x2128u);
    unsigned filled=(width*percent+50u)/100u;
    if(filled){display_rect(x,y,filled,4u,0x05B8u);if(filled>4u){unsigned shimmer=(phase/3u)%filled;if(shimmer+1u<filled)display_rect(x+shimmer,y,2u,4u,0x7FFFu);}}
}
void display_frame_fx(uint32_t phase,uint8_t alert)
{
    static const uint16_t colors[6]={0x07FFu,0x17FFu,0x07E0u,0xFFE0u,0xF81Fu,0x07FFu};
    uint16_t glow=alert?0xF800u:colors[(phase/5u)%6u];
    display_rect(0u,0u,SCREEN_W,2u,glow);
    display_rect(0u,SCREEN_H-2u,SCREEN_W,2u,colors[(phase/5u+3u)%6u]);
    unsigned star=(phase/2u)%SCREEN_W;
    display_rect(star,2u,2u,2u,0xFFFFu);
    display_rect((SCREEN_W-1u)-star,SCREEN_H-4u,2u,2u,0xFFFFu);
}
static void lcd_init(void)
{
    pin_high(gpio_b,LCD_CS_PIN);pin_low(gpio_b,LCD_RESET_PIN);delay_ms(20u);pin_high(gpio_b,LCD_RESET_PIN);delay_ms(120u);
    lcd_command(0x01u);lcd_end();delay_ms(120u);lcd_command(0x11u);lcd_end();delay_ms(120u);
    const uint8_t fr[]={0x01u,0x2Cu,0x2Du},inv[]={0x07u};
    const uint8_t pwr1[]={0xA2u,0x02u,0x84u},pwr2[]={0xC5u},pwr3[]={0x0Au,0x00u};
    const uint8_t pwr4[]={0x8Au,0x2Au},pwr5[]={0x8Au,0xEEu},vcom[]={0x0Eu};
    const uint8_t madctl[]={ (uint8_t)(0xC0u|(LCD_RGB_ORDER?0x00u:0x08u)) },fmt[]={0x05u};
    lcd_cmd_data(0xB1u,fr,3u);lcd_cmd_data(0xB2u,fr,3u);
    const uint8_t fr3[]={0x01u,0x2Cu,0x2Du,0x01u,0x2Cu,0x2Du};lcd_cmd_data(0xB3u,fr3,6u);
    lcd_cmd_data(0xB4u,inv,1u);lcd_cmd_data(0xC0u,pwr1,3u);lcd_cmd_data(0xC1u,pwr2,1u);lcd_cmd_data(0xC2u,pwr3,2u);lcd_cmd_data(0xC3u,pwr4,2u);lcd_cmd_data(0xC4u,pwr5,2u);lcd_cmd_data(0xC5u,vcom,1u);
    lcd_cmd_data(0x36u,madctl,1u);lcd_cmd_data(0x3Au,fmt,1u);
#if LCD_INVERT
    lcd_command(0x21u);
#else
    lcd_command(0x20u);
#endif
    lcd_end();lcd_command(0x13u);lcd_end();lcd_command(0x29u);lcd_end();delay_ms(20u);display_clear(0x0861u);
}
void display_init(void){lcd_init();}

