#ifndef _CRC_H_
#define _CRC_H_

extern void build_table_crc16();
extern unsigned short compute_crc16(char *bytes, size_t size);
extern void release_table_crc16();

#endif // _CRC_H_
