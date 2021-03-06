
/*
@module  gpio
@summary GPIO操作
@version 1.0
@data    2020.03.30
*/
#include "luat_base.h"
#include "luat_log.h"
#include "luat_gpio.h"
#include "luat_malloc.h"

static int l_gpio_close(lua_State *L);

#define GPIO_IRQ_COUNT 16
typedef struct luat_lib_gpio_cb
{
    int pin;
    int lua_ref;
} luat_lib_gpio_cb_t;

static luat_lib_gpio_cb_t irq_cbs[GPIO_IRQ_COUNT];

int l_gpio_handler(lua_State *L, void* ptr) {
    // 给 sys.publish方法发送数据
    rtos_msg_t* msg = (rtos_msg_t*)lua_topointer(L, -1);
    int pin = msg->arg1;
    for (size_t i = 0; i < GPIO_IRQ_COUNT; i++)
    {
        if (irq_cbs[i].pin == pin) {
            lua_geti(L, LUA_REGISTRYINDEX, irq_cbs[i].lua_ref);
            if (!lua_isnil(L, -1)) {
                lua_pushinteger(L, msg->arg2);
                lua_call(L, 1, 0);
            }
            return 0;
        }
    }
    return 0;
}

/*
设置管脚功能
@funtion gpio.setup(pin, mode, pull)
@int pin 针脚编号,必须是数值
@any mode 输入输出模式. 数字0/1代表输出模式,nil代表输入模式,function代表中断模式
@int pull 上拉下列模式, 可以是gpio.PULLUP 或 gpio.PULLDOWN, 需要根据实际硬件选用
@int irq 中断触发模式, 上升沿gpio.RISING, 下降沿gpio.FALLING, 上升和下降都要gpio.BOTH.默认是RISING
@return any 输出模式返回设置电平的闭包, 输入模式和中断模式返回获取电平的闭包
@usage 
-- 设置gpio17为输入
gpio.setup(17, nil) 
@usage 
-- 设置gpio17为输出
gpio.setup(17, 0) 
@usage 
-- 设置gpio27为中断
gpio.setup(27, function(val) print("IRQ_27") end, gpio.RISING)
*/
static int l_gpio_setup(lua_State *L) {
    //lua_gettop(L);
    // TODO 设置失败会内存泄漏
    luat_gpio_t conf;
    conf.pin = luaL_checkinteger(L, 1);
    //conf->mode = luaL_checkinteger(L, 2);
    conf.lua_ref = 0;
    if (lua_isfunction(L, 2)) {
        conf.mode = Luat_GPIO_IRQ;
        lua_pushvalue(L, 2);
        conf.lua_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    else if (lua_isinteger(L, 2)) {
        conf.mode = Luat_GPIO_OUTPUT;
    }
    else {
        conf.mode = Luat_GPIO_INPUT;
    }
    conf.pull = luaL_optinteger(L, 3, Luat_GPIO_DEFAULT);
    conf.irq = luaL_optinteger(L, 4, Luat_GPIO_BOTH);
    int re = luat_gpio_setup(&conf);
    if (re == 0) {
        int flag = 1;
        for (size_t i = 0; i < GPIO_IRQ_COUNT; i++) {
            if (irq_cbs[i].pin == conf.pin) {
                if (irq_cbs[i].lua_ref && irq_cbs[i].lua_ref != conf.lua_ref) {
                    luaL_unref(L, LUA_REGISTRYINDEX, irq_cbs[i].lua_ref);
                    irq_cbs[i].lua_ref = conf.lua_ref;
                }
                flag = 0;
                break;
            }
            if (irq_cbs[i].pin == 0) {
                irq_cbs[i].pin = conf.pin;
                irq_cbs[i].lua_ref = conf.lua_ref;
                flag = 0;
                break;
            }
        }
        if (flag) {
            luat_log_warn("luat.gpio", "too many irq setup!!!!");
            re = 1;
            luat_gpio_close(conf.pin);
        }
    }
    //luat_heap_free(conf);
    lua_pushinteger(L, re == 0 ? 1 : 0);
    return 1;
}

/*
设置管脚电平
@api gpio.set(pin, value)
@int pin 针脚编号,必须是数值
@int value 电平, 可以是 高电平gpio.HIGH, 低电平gpio.LOW, 或者直接写数值1或0
@return nil
@usage
-- 设置gpio17为低电平
gpio.set(17, 0) 
*/
static int l_gpio_set(lua_State *L) {
    luat_gpio_set(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2));
    return 0;
}

/*
获取管脚电平
@api gpio.get(pin)
@int pin 针脚编号,必须是数值
@return value 电平, 高电平gpio.HIGH, 低电平gpio.LOW, 对应数值1和0
@usage 
-- 获取gpio17的当前电平
gpio.get(17) 
*/
static int l_gpio_get(lua_State *L) {
    lua_pushinteger(L, luat_gpio_get(luaL_checkinteger(L, 1)) & 0x01 ? 1 : 0);
    return 1;
}

/*
关闭管脚功能(高阻输入态),关掉中断
@api gpio.close(pin)
@int pin 针脚编号,必须是数值
@return nil 无返回值,总是执行成功
@usage
-- 关闭gpio17
gpio.close(17)
*/
static int l_gpio_close(lua_State *L) {
    int pin = luaL_checkinteger(L, 1);
    luat_gpio_close(pin);
    for (size_t i = 0; i < GPIO_IRQ_COUNT; i++) {
        if (irq_cbs[i].pin == pin) {
            irq_cbs[i].pin = 0;
            if (irq_cbs[i].lua_ref) {
                luaL_unref(L, LUA_REGISTRYINDEX, irq_cbs[i].lua_ref);
                irq_cbs[i].lua_ref = 0;
            }
        }
    }
    return 0;
}

#include "rotable.h"
static const rotable_Reg reg_gpio[] =
{
    { "setup" ,         l_gpio_setup ,0},
    { "set" ,           l_gpio_set,   0},
    { "get" ,           l_gpio_get,   0 },
    { "close" ,         l_gpio_close, 0 },
    { "LOW",            NULL,         Luat_GPIO_LOW},
    { "HIGH",           NULL,         Luat_GPIO_HIGH},

    { "OUTPUT",         NULL,         Luat_GPIO_OUTPUT},
    { "INPUT",          NULL,         Luat_GPIO_INPUT},
    { "IRQ",            NULL,         Luat_GPIO_IRQ},

    { "PULLUP",         NULL,         Luat_GPIO_PULLUP},
    { "PULLDOWN",       NULL,         Luat_GPIO_PULLDOWN},

    { "RISING",         NULL,         Luat_GPIO_RISING},
    { "FALLING",        NULL,         Luat_GPIO_FALLING},
    { "BOTH",           NULL,         Luat_GPIO_BOTH},
	{ NULL,             NULL ,        0}
};

LUAMOD_API int luaopen_gpio( lua_State *L ) {
    rotable_newlib(L, reg_gpio);
    return 1;
}
