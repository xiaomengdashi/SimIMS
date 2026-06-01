#ifndef MILENAGE_ALGO_H_INCLUDED
#define MILENAGE_ALGO_H_INCLUDED
 
typedef unsigned char BYTE; 
 
 
/*--------------------------- prototypes --------------------------*/ 
void f1( BYTE op_c[16], BYTE key[16], BYTE rand[16], BYTE sqn[6], BYTE amf[2], BYTE mac_a[8] );
void KeyAdd(BYTE state[4][4], BYTE roundKeys[11][4][4], int round);
int ByteSub(BYTE state[4][4]);
void f2345 ( BYTE op_c[16], BYTE k[16], BYTE rand[16], BYTE res[8], BYTE ck[16], BYTE ik[16], BYTE ak[6] ); 
void f1star(BYTE op_c[16], BYTE k[16], BYTE rand[16], BYTE sqn[6], BYTE amf[2], BYTE mac_s[8] ); 
void f5star( BYTE op_c[16], BYTE k[16], BYTE rand[16], BYTE ak[6] ); 
void KeyAdd(BYTE state[4][4], BYTE roundKeys[11][4][4], int round);
void MixColumn(BYTE state[4][4]);
void ComputeOPc( BYTE op[16], BYTE key[16], BYTE op_c[16] ); 
void RijndaelKeySchedule( BYTE key[16] ); 
void RijndaelEncrypt( BYTE input[16], BYTE output[16] );
void ShiftRow(BYTE state[4][4]);
 
#endif