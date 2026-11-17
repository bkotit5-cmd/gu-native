// Move to skip the flex ([Comforts] FlexCancel, runtime/hle/flexcancel.c)
#pragma once

void flexcancel_init(void);         // hooks the flex's update and the hunter's commands (before the game starts)
int config_live_flex_cancel(void);  // [Comforts] FlexCancel as saved, or as the menu just set it (runtime/config_store.c)
