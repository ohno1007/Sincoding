// motor.h — 一个示例外部 C 库的头文件
#ifndef MOTOR_H
#define MOTOR_H
void motor_set_speed(int v);   // 设置转速
int  motor_get_speed(void);    // 读取转速
double motor_scale(double x);  // 把输入放大 1.5 倍（演示 float/double）
#endif
