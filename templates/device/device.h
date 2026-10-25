/* ${NAME}: what programs need to use ${LOWER}.device. */
#ifndef ${UPPER}_DEVICE_H
#define ${UPPER}_DEVICE_H

#include <exec/io.h>

#define ${UPPER}_NAME     "${LOWER}.device"
#define ${UPPER}_BUFSIZE  256       /* the device keeps this many bytes */

/* Commands: CMD_WRITE stores io_Length bytes from io_Data (at io_Offset), CMD_READ copies
 * them back, CMD_CLEAR zeroes the buffer, NSCMD_DEVICEQUERY says what it supports. */

#endif
