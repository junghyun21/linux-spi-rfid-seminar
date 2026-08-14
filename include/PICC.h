#ifndef PICC_H
#define PICC_H

#define REQA_TIMEOUT_MS         5000        // 5ms (임의)
#define ANTICOLL_TIMEOUT_MS     5000        // 5ms (임의)
#define SELECT_TIMEOUT_MS       5000        // 5ms (임의)
#define AUTH_TIMEOUT_MS         1000        // 1ms
#define READ_TIMEOUT_MS         5000        // 5ms


// Command
#define PICC_REQA               0x26        // Request
#define PICC_READ               0x30        // MIFARE Read
#define PICC_ANTICOLL           0x20
#define PICC_SELECT             0x70

// value
#define PICC_CT                 0x88
#define PICC_SEL_CL1            0x93        // NVB(Number of Valid Bits)
#define PICC_SEL_CL2            0x95        // NVB(Number of Valid Bits)
#define PICC_MF_AUTH_KEY_A      0x60

#define SAK_CASCADE_BIT   0x04


#endif