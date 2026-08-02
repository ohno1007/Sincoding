// motor.c — 示例外部库实现（编译为 libmotor.so / libmotor.a）
#include "motor.h"
#include <stdio.h>
static int g_speed = 0;
void motor_set_speed(int v) { g_speed = v; printf("[motor] 转速设为 %d\n", v); }
int  motor_get_speed(void)  { return g_speed; }
double motor_scale(double x) { return x * 1.5; }
