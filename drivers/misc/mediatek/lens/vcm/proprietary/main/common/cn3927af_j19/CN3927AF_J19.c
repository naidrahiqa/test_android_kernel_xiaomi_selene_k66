// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015 MediaTek Inc.
 * Copyright (c) 2021 XiaoMi, Inc.
 */

/*
 * CN3927AF_J19 voice coil motor driver
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "lens_info.h"

#define AF_DRVNAME "CN3927AF_DRV"
#define AF_I2C_SLAVE_ADDR 0x18

#define LOG_INF(format, args...)                                               \
	pr_debug(AF_DRVNAME " [%s] " format, __func__, ##args)

static struct i2c_client *g_pstAF_I2Cclient;
static int *g_pAF_Opened;
static spinlock_t *g_pAF_SpinLock;

static unsigned long g_u4AF_INF;
static unsigned long g_u4AF_MACRO = 1023;
static unsigned long g_u4TargetPosition;
static unsigned long g_u4CurrPosition;

static int i2c_read(u8 a_u2Addr, u8 *a_puBuff)
{
	int ret;
	u8 cmd = a_u2Addr;

	g_pstAF_I2Cclient->addr = AF_I2C_SLAVE_ADDR >> 1;

	ret = i2c_master_send(g_pstAF_I2Cclient, &cmd, 1);
	if (ret != 1) {
		LOG_INF("I2C write failed!!\n");
		return -1;
	}

	ret = i2c_master_recv(g_pstAF_I2Cclient, a_puBuff, 1);
	if (ret != 1) {
		LOG_INF("I2C read failed!!\n");
		return -1;
	}

	return 0;
}

static u8 read_data(u8 addr)
{
	u8 get_byte = 0;

	i2c_read(addr, &get_byte);
	return get_byte;
}

static int s4AF_ReadReg(unsigned short *a_pu2Result)
{
	*a_pu2Result = (read_data(0x03) << 8) + (read_data(0x04) & 0xff);
	return 0;
}

static int s4AF_WriteReg(u16 a_u2Data)
{
	int ret;
	u8 cmd[3] = { 0x03, (u8)(a_u2Data >> 8), (u8)(a_u2Data & 0xFF) };

	g_pstAF_I2Cclient->addr = AF_I2C_SLAVE_ADDR >> 1;

	ret = i2c_master_send(g_pstAF_I2Cclient, cmd, 3);
	if (ret < 0) {
		LOG_INF("I2C send failed!!\n");
		return -1;
	}

	return 0;
}

static int s4AF_PowerDown(void)
{
	int ret;
	u8 cmd_advance[2] = { 0xED, 0xAB };
	u8 cmd_release[2] = { 0x02, 0x01 };

	if (!g_pstAF_I2Cclient)
		return -ENODEV;

	g_pstAF_I2Cclient->addr = AF_I2C_SLAVE_ADDR >> 1;

	ret = i2c_master_send(g_pstAF_I2Cclient, cmd_advance, 2);
	if (ret < 0) {
		LOG_INF("I2C send failed!!\n");
		return -1;
	}

	ret = i2c_master_send(g_pstAF_I2Cclient, cmd_release, 2);
	if (ret < 0) {
		LOG_INF("I2C send failed!!\n");
		return -1;
	}

	return 0;
}

static inline int getAFInfo(__user struct stAF_MotorInfo *pstMotorInfo)
{
	struct stAF_MotorInfo stMotorInfo;

	stMotorInfo.u4MacroPosition = g_u4AF_MACRO;
	stMotorInfo.u4InfPosition = g_u4AF_INF;
	stMotorInfo.u4CurrentPosition = g_u4CurrPosition;
	stMotorInfo.bIsSupportSR = 1;
	stMotorInfo.bIsMotorMoving = 1;
	stMotorInfo.bIsMotorOpen = (*g_pAF_Opened >= 1) ? 1 : 0;

	if (copy_to_user(pstMotorInfo, &stMotorInfo,
			 sizeof(struct stAF_MotorInfo)))
		LOG_INF("copy to user failed when getting motor information\n");

	return 0;
}

static int initdrv(void)
{
	int ret;
	u8 cmd0[2] = { 0xED, 0xAB };
	u8 cmd1[2] = { 0x02, 0x01 };
	u8 cmd2[2] = { 0x02, 0x00 };
	u8 cmd3[2] = { 0x06, 0x84 };
	u8 cmd4[2] = { 0x07, 0x01 };
	u8 cmd5[2] = { 0x08, 0x24 };

	g_pstAF_I2Cclient->addr = AF_I2C_SLAVE_ADDR >> 1;
	i2c_master_send(g_pstAF_I2Cclient, cmd0, 2);
	i2c_master_send(g_pstAF_I2Cclient, cmd1, 2);
	i2c_master_send(g_pstAF_I2Cclient, cmd2, 2);
	mdelay(1);
	i2c_master_send(g_pstAF_I2Cclient, cmd3, 2);
	i2c_master_send(g_pstAF_I2Cclient, cmd4, 2);
	ret = i2c_master_send(g_pstAF_I2Cclient, cmd5, 2);
	return ret;
}

static inline int moveAF(unsigned long a_u4Position)
{
	int ret = 0;

	if (a_u4Position > g_u4AF_MACRO || a_u4Position < g_u4AF_INF) {
		LOG_INF("out of range\n");
		return -EINVAL;
	}

	if (*g_pAF_Opened == 1) {
		unsigned short init_pos = 0;

		if (initdrv() >= 0) {
			spin_lock(g_pAF_SpinLock);
			*g_pAF_Opened = 2;
			spin_unlock(g_pAF_SpinLock);
		} else {
			LOG_INF("VCM driver init fail\n");
		}
		ret = s4AF_ReadReg(&init_pos);

		spin_lock(g_pAF_SpinLock);
		g_u4CurrPosition = (ret == 0) ? (unsigned long)init_pos : 0;
		spin_unlock(g_pAF_SpinLock);
	}

	if (g_u4CurrPosition == a_u4Position)
		return 0;

	spin_lock(g_pAF_SpinLock);
	g_u4TargetPosition = a_u4Position;
	spin_unlock(g_pAF_SpinLock);

	if (s4AF_WriteReg((u16)g_u4TargetPosition) == 0) {
		spin_lock(g_pAF_SpinLock);
		g_u4CurrPosition = g_u4TargetPosition;
		spin_unlock(g_pAF_SpinLock);
		ret = 0;
	} else {
		LOG_INF("set I2C failed when moving the motor\n");
		ret = -1;
	}

	return ret;
}

static inline int setAFInf(unsigned long a_u4Position)
{
	spin_lock(g_pAF_SpinLock);
	g_u4AF_INF = a_u4Position;
	spin_unlock(g_pAF_SpinLock);
	return 0;
}

static inline int setAFMacro(unsigned long a_u4Position)
{
	spin_lock(g_pAF_SpinLock);
	g_u4AF_MACRO = a_u4Position;
	spin_unlock(g_pAF_SpinLock);
	return 0;
}

long CN3927AFJ19_Ioctl(struct file *a_pstFile, unsigned int a_u4Command,
		       unsigned long a_u4Param)
{
	long ret = 0;

	switch (a_u4Command) {
	case AFIOC_G_MOTORINFO:
		ret = getAFInfo((__user struct stAF_MotorInfo *)(a_u4Param));
		break;
	case AFIOC_T_MOVETO:
		ret = moveAF(a_u4Param);
		break;
	case AFIOC_T_SETINFPOS:
		ret = setAFInf(a_u4Param);
		break;
	case AFIOC_T_SETMACROPOS:
		ret = setAFMacro(a_u4Param);
		break;
	default:
		LOG_INF("No CMD\n");
		ret = -EPERM;
		break;
	}

	return ret;
}

int CN3927AFJ19_Release(struct inode *a_pstInode, struct file *a_pstFile)
{
	u8 cmd_release[2] = { 0x02, 0x01 };
	u8 cmd_move_release[2] = { 0x06, 0x83 };
	unsigned long curr_position = g_u4CurrPosition;

	LOG_INF("Start\n");

	if (*g_pAF_Opened == 2) {
		g_pstAF_I2Cclient->addr = AF_I2C_SLAVE_ADDR >> 1;

		if (i2c_master_send(g_pstAF_I2Cclient, cmd_move_release, 2) < 0)
			LOG_INF("CN3927AFJ19 move release fail!\n");

		if (curr_position > 400) {
			s4AF_WriteReg(400);
			mdelay(12);
			curr_position = 400;
		}
		if (curr_position > 300) {
			s4AF_WriteReg(300);
			mdelay(12);
			curr_position = 300;
		}
		while (curr_position > 100) {
			curr_position -= 50;
			s4AF_WriteReg((u16)curr_position);
			mdelay(12);
		}

		if (i2c_master_send(g_pstAF_I2Cclient, cmd_release, 2) < 0)
			LOG_INF("CN3927AFJ19 release fail!\n");
	}

	if (*g_pAF_Opened) {
		LOG_INF("Free\n");
		spin_lock(g_pAF_SpinLock);
		*g_pAF_Opened = 0;
		spin_unlock(g_pAF_SpinLock);
	}

	LOG_INF("End\n");
	return 0;
}

int CN3927AFJ19_PowerDown(struct i2c_client *pstAF_I2Cclient, int *pAF_Opened)
{
	g_pstAF_I2Cclient = pstAF_I2Cclient;
	g_pAF_Opened = pAF_Opened;

	return s4AF_PowerDown();
}

int CN3927AFJ19_SetI2Cclient(struct i2c_client *pstAF_I2Cclient,
			     spinlock_t *pAF_SpinLock, int *pAF_Opened)
{
	g_pstAF_I2Cclient = pstAF_I2Cclient;
	g_pAF_SpinLock = pAF_SpinLock;
	g_pAF_Opened = pAF_Opened;

	return 1;
}

int CN3927AFJ19_GetFileName(unsigned char *pFileName)
{
#if SUPPORT_GETTING_LENS_FOLDER_NAME
	char file_path[256];
	char *slash;

	scnprintf(file_path, sizeof(file_path), "%s", __FILE__);
	slash = strrchr(file_path, '/');
	if (slash) {
		*slash = '\0';
		slash = strrchr(file_path, '/');
	}
	if (slash)
		strscpy(pFileName, slash + 1, AF_MOTOR_NAME);
	else
		pFileName[0] = '\0';
#else
	pFileName[0] = '\0';
#endif
	return 1;
}
