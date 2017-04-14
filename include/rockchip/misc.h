#ifndef __ROCKCHIP_MISC_H__
#define __ROCKCHIP_MISC_H__

#ifdef CONFIG_CMD_BMP
int misc_init_r(void);
int draw_logo(void);
#endif

#endif /* __ROCKCHIP_MISC_H__ */
