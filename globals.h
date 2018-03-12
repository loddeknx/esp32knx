/*
 * globals.h
 *
 *  Created on: 04.03.2018
 *      Author: lars
 */

#ifndef MAIN_GLOBALS_H_
#define MAIN_GLOBALS_H_


#ifndef GA(a,b,c)
#define GA(a,b,c)    (uint16_t) (((uint16_t)(uint8_t)(a & 0x1F)<<11)|((uint16_t)(uint8_t)(b & 0x07)<<8)|(uint8_t)(c & 0xFF))
#endif

inline uint16_t parseGA(char* cGroupAddr){
	int nA,nB,nC;
	sscanf(cGroupAddr, "%d/%d/%d", &nA, &nB, &nC);
	return (GA(nA, nB, nC));
}


static char* GAasString(unsigned short add) {
	static char cTmp[16];
	int nA = (add & 0xf800) >> 11;      // 5 bits (0..31)  2 digits
	int nB = (add & 0x0700) >> 8;       // 3 bits (0..7)   1 digit
	int nC = (add & 0x00ff);            // 8 bits (0..255) 3 digits
	sprintf(cTmp, "%.2d/%.1d/%.3d", nA, nB, nC);
	return cTmp;
}


#endif /* MAIN_GLOBALS_H_ */
