/**
 * @file    raspchar.h
 * @author  PHAM Minh Thuc
 * @date    14 April 2020
 * @version 0.1
 * @brief   Contains the definitions for describing the device virtual raspchar. Raspchar device is on RAM
*/
#define REG_SIZE 1 // size of register 1 byte (8 bits)
#define NUM_CTRL_REGS 1 //number of controller registers
#define NUM_STS_REGS 5 //number of status registers
#define NUM_DATA_REGS 256 //number of data registers
#define NUM_DEV_REGS (NUM_CTRL_REGS + NUM_STS_REGS + NUM_DATA_REGS) //total of registers

/****************** Description of status register: START ******************/
/*
 *  [READ_COUNT_H_REG:READ_COUNT_L_REG]:
 * - value initial: 0x0000
 * - Everytime read sucessful on data register -> increase 1 unit
 */
#define READ_COUNT_H_REG 0
#define READ_COUNT_L_REG 1

/*
 *  [WRITE_COUNT_H_REG:WRITE_COUNT_L_REG]:
 * - value initial: 0x0000
 * - Everytime write sucessful on data register -> increase 1 unit
 */
#define WRITE_COUNT_H_REG 2
#define WRITE_COUNT_L_REG 3

/*
 * register DEVICE_STATUS_REG:
 * - value initial: 0x03
 * - bit:
 *   bit0:
 *       0: inform that the data registers is not ready to read 
 *       1: inform that the data registers is ready to read
 *   bit1:
 *       0: inform that the data registers is not ready to write
 *       1: inform that the data registers is ready to read
 *   bit2:
 *       0: all registers are cleared -> bit set to 0
 *       1: all registers are writen -> bit set to 1
 *   bit3~7: do not use
 */
#define DEVICE_STATUS_REG 4

#define STS_READ_ACCESS_BIT (1 << 0)
#define STS_WRITE_ACCESS_BIT (1 << 1)
#define STS_DATAREGS_OVERFLOW_BIT (1 << 2)

#define READY 1
#define NOT_READY 0
#define OVERFLOW 1
#define NOT_OVERFLOW 0
/****************** Description for status registers: END ******************/


/****************** Description for controller register: START ******************/
/*
 * register CONTROL_ACCESS_REG:
 * @brief bits control the permit of read/write on data registers
 * - value initial: 0x03
 * - bits:
 *   bit0:
 *       0: Not allow reading from the data registers
 *       1: Allow reading from the data registers
 *   bit1:
 *       0: Not allow writing from the data registers
 *       1: allow writing from the data registers
 *   bit2~7: Do not use
 */
#define CONTROL_ACCESS_REG 0

#define CTRL_READ_DATA_BIT (1 << 0)
#define CTRL_WRITE_DATA_BIT (1 << 1)

#define ENABLE 1
#define DISABLE 0
/****************** Description controller register: END ******************/