#ifndef NVFD_DISPLAY_H
#define NVFD_DISPLAY_H

void display_help(void);
/* Returns 0, or -1 if the config file could not be read. */
int  display_status(void);
void display_list_gpus(void);
/* Returns 0, or -1 if the curve file exists but is invalid. */
int  display_fan_curve(void);

#endif /* NVFD_DISPLAY_H */
