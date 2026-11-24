#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USER_SHADER_PARAM_MAX 16

enum {
    USER_SHADER_PARAM_FLOAT = 0,
    USER_SHADER_PARAM_INT = 1,
    USER_SHADER_PARAM_BOOL = 2
};

typedef struct {
    char name[48];
    char label[80];
    int type;
    float value;
    float initial;
    float minimum;
    float maximum;
    float step;
} UserShaderParam;

int vk_user_shader_param_count(void);
bool vk_user_shader_param_get(int index, UserShaderParam *out);
bool vk_user_shader_param_set(int index, float value);
void vk_user_shader_params_reset(void);
void vk_user_shader_params_pack(float values[USER_SHADER_PARAM_MAX][4]);
void vk_user_shader_hud_capture_enable(bool enabled);

#ifdef __cplusplus
}
#endif
