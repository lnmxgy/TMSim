
/*tumsim--- the program of subclone simulator for data generator file(*.sim or *_sunchged.idx)
 *Several subclones were usually found in cancer cells. This program can run multiple times, 
 *it can respectively realizes to set variation of sites of all subclones .
 *In this, considering inheritance  and mutual exclusivity of the variation between different subclones. */

#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <iostream> 
#include <fstream>

using namespace std;

int 	REF_LEN=0;
float	SV_MUT_RATE=0.00001;
float	ERR_RATE=0.00;
float 	INDEL_FRAC=0.00;
float 	DEL_RATE= 0.5;
float	INDEL_EXTEND = 0.3;
float	BB_RATE = 0.33333;
float	LOH_BB_RATE = 0.5;
float	LOH_NOR_AB_RATE = 0.00001;

int 	LONG_INDEL = 0;
int 	HIGH_DENSITY = 0;
int 	OTHER_CHGED = 0;
int 	OUT_CHGED = 0;
char 	INOCHGFs[10][50];

FILE* ingenfp;
FILE* insimfp;
FILE* innorabfp;
FILE* indelfp;
FILE* highfp;
FILE* outindelfp;
FILE* inochgfp;
FILE* outsimfp;
FILE* outchgfp;
FILE* outresult;
FILE* outsABp;
FILE* outLOHp;

#define INIT_SEQ(seq) (seq).s = 0; (seq).l = (seq).max = 0
#define CLEAR_SEQ(seq) free( (seq).s )
#define CLEAR_MUTSEQ(mseq) free( (mseq).s )

enum muttype_t {NOCHANGE = 0, INSERT = 0x80, SUBSTITUTE = 0x40, DELETE = 0xc0, GTYPEBB=0x20, 
				GTYPEAB=0x10, GTYPELOH=0x30, LONGINSERT = 0xf0000000, NOTDELETE = 0xffffff3f };
typedef unsigned int mut_t;
static mut_t mutmsk = (mut_t)0xc0;
static int SEQ_BLOCK_SIZE=512;

typedef struct {
	int l, max;
	char *s;
} seq_t;

typedef struct {
	int l,max, v;  //v---valid char number, so as A C G T
	unsigned int *s;
} mutseq_t;

typedef struct {
	int indel_no;
	int indel_beg, indel_len, indel_type, indel_num, indel_var;
	char *str;
} indel_e;

typedef struct {
	int l, max;
	indel_e *s;
} indel_t;
typedef struct {
	int no;
	int beg, len, mark, num, dalt;
} high_e;
typedef struct {
	int l, max;
	high_e *s;
} high_t;

/**********************************************/
//int MAXSS = pow(2, 31)-1;
static double zrand(){
	static double r;
	int n;
	n = rand() % RAND_MAX;
	r = 1.0*n/RAND_MAX;
	return r;
}


/**********************************************/
static double abszzm(double x){
	if (x<0) return -x;
	return x;
}
/**********************************************/
static double zt_stdfun(){
	static double v1,v2,s;
	do {
		v1 = zrand();
		v2 = zrand();
		s = 1.0*cos(2*3.1415926*v1)*sqrt(-2*log(v2));
	} while(s>2 || s<-2);
	return s;
}


/**********************************************/
FILE *FileOpen(char *fileName, const char *mode)
{
	FILE *fp;
	fp = fopen (fileName, mode);
	if (fp == NULL)
	{
		fprintf(stdout, "Error: Cannot Open the file %s for %s !\n", fileName, mode);
		fflush(stdout);
		exit(0);
	}
	return fp;
}

/**********************************************/
int gch_to_num(char orgch){
	int ch;
	orgch = _toupper(orgch);
	switch(orgch){
		case 'A': ch=0; break;
		case 'C': ch=1; break;
		case 'G': ch=2; break;
		case 'T': ch=3; break;
		default : ch=4; break;
	}
	return ch;
}

/**********************************************/
char num_to_gch(int orgch){
	char ch;
	switch(orgch){
		case 0:   ch='A'; break;
		case 1:   ch='C'; break;
		case 2:   ch='G'; break;
		case 3:   ch='T'; break;
		default : ch='N'; break;
	}
	return ch;
}


///**************************************************************
int seq_read_fasta(FILE *fp, seq_t *seq, char *chrname, char *comment)
{
	int c, l, max, n;
	char *p;
	
	c = 0;
	while (!feof(fp) && fgetc(fp) != '>');
	if (feof(fp)) return -1;
	p = chrname;
	while (!feof(fp) && (c = fgetc(fp)) != ' ' && c != '\t' && c != '\n')
		if (c != '\r') *p++ = c;
	*p = '\0';
	if (comment) {
		p = comment;
		if (c != '\n') {
			while (!feof(fp) && ((c = fgetc(fp)) == ' ' || c == '\t'));
			if (c != '\n') {
				*p++ = c;
				while (!feof(fp) && (c = fgetc(fp)) != '\n')
					if (c != '\r') *p++ = c;
			}
		}
		*p = '\0';
	} else if (c != '\n') while (!feof(fp) && fgetc(fp) != '\n');
	n=0;
	l = 0; max = seq->max;
	while (!feof(fp) && (c = fgetc(fp)) ) {
		if (isalpha(c) || c == '-' || c == '.') {
			if (l + 1 >= max) {
				max += SEQ_BLOCK_SIZE;
				seq->s = (char *)realloc(seq->s, sizeof(char) * max);
			}
			seq->s[l++] = _toupper((char)c);
			if (seq->s[l-1]=='A' ||seq->s[l-1]=='C'||seq->s[l-1]=='G'||seq->s[l-1]=='T') n++;
		}
	}
	seq->s[l] = 0;
	seq->max = max; 
	seq->l = l;
	return l;
}

/**********************************************/
int set_pos_sim(mutseq_t *hap1, mutseq_t *hap2, seq_t *nochged, int k){  // return chged num
	int deleting = 0;
	mutseq_t *ret[2];
	int tmp, num, m, max, chgnum;
	double r;
	int c0, c1, c;

	ret[0] = hap1; ret[1] = hap2; m=0;
	max = hap1->l;
	c0 = ret[0]->s[k];
	c1 = ret[1]->s[k];
	chgnum = 1;
	if (zrand() >= INDEL_FRAC) { // substitution
		c = (c0 + (int)(zrand()* 3.0 + 1)) & 3;
		if (zrand() < BB_RATE) { // hom
			ret[0]->s[k] = SUBSTITUTE|GTYPEBB|c;
			ret[1]->s[k] = SUBSTITUTE|GTYPEBB|c;
		} else { // het
			tmp = zrand()<0.5?0:1;
			ret[tmp]->s[k] = SUBSTITUTE|GTYPEAB|c;
		}
	} else { // indel
		if (zrand() < DEL_RATE) { // deletion
			if (zrand() < BB_RATE ) { // hom-del
				ret[0]->s[k] |= DELETE|GTYPEBB;
				ret[1]->s[k] |= DELETE|GTYPEBB;
				deleting = 3;
			} else { // het-del
				deleting = zrand()<0.5?1:2;
				ret[deleting-1]->s[k] |= DELETE|GTYPEAB;
			}
			k++;
			c0 = ret[0]->s[k];
			c1 = ret[1]->s[k];
			while(zrand() < INDEL_EXTEND  && k < max && c0<4 && c1<4 && nochged->s[k]==0 ) {
				if (deleting<3) ret[deleting-1]->s[k] |= DELETE|GTYPEAB; 
        		else ret[0]->s[k] = ret[1]->s[k] |= DELETE|GTYPEBB; 
				chgnum++;
				nochged->s[k] |= 4;
				k++; 
				c0 = ret[0]->s[k];
				c1 = ret[1]->s[k];
			}
		} else { // insertion
			int num_ins = 0, ins = 0;
			do {
				num_ins++;
			       	ins = (ins << 2) | (int)(zrand() * 4.0);
			} while (num_ins < 10 && zrand() < INDEL_EXTEND);
			chgnum = num_ins; //insert num counting;
			if (zrand() < BB_RATE ) { // hom-ins
				ret[0]->s[k] = ret[1]->s[k] = (num_ins << 28) | (ins << 8) | INSERT | GTYPEBB| c0;
			} else { // het-ins
				tmp = zrand()<0.5?0:1;
				ret[tmp]->s[k] = (num_ins << 28) | (ins << 8) | INSERT | GTYPEAB | c0;
			}
		}
	} //end indel // mutation
	return chgnum;
}


/*******************************************************/
void set_mut_sim(seq_t *seq, mutseq_t *hap1, mutseq_t *hap2, seq_t *nochged, mutseq_t *norab, high_t *highp)
{
	mutseq_t *ret[2];
	int num, i, j, k, n, m, max;
	double r;
	mut_t c0, c1, c;
	int hdno, reng, hdbeg;

	srand(unsigned (time(0)));  // Init Random data // 
	ret[0] = hap1; ret[1] = hap2; m=0;
	max = ret[0]->l;
	num = (int)(ret[0]->v * SV_MUT_RATE);

	printf("Enter set_mut_sim max=%d   num=%d \n", max, num);
	n = 0; 
	while(n < num){ //set mutation and not normal AB mode
		k = zrand()*max;
		c0 = ret[0]->s[k];
		c1 = ret[1]->s[k];
		while (c0 >= 4 || c1 >= 4 || nochged->s[k] ){
			k = zrand()*max;
			c0 = ret[0]->s[k];
			c1 = ret[1]->s[k];
		}
		nochged->s[k] |= 4;
		n += set_pos_sim( hap1, hap2, nochged, k);
	}  //end of while(n < num)

	//High density area set
	printf("\tEnter High density area set");
	
	if (HIGH_DENSITY) { 
		for(i=0; i<highp->l; i++) {
			switch(highp->s[i].mark){
			case 0: 
				n = 0; 
				while(n < highp->s[i].num){ //set mutation in High density area
					k = highp->s[i].beg + zrand()*highp->s[i].len; // random in High density area
					c0 = ret[0]->s[k];
					c1 = ret[1]->s[k];
					while (c0 >= 4 || c1 >= 4 || nochged->s[k]){
						k = highp->s[i].beg + zrand()*highp->s[i].len;
						c0 = ret[0]->s[k];
						c1 = ret[1]->s[k];
					}	
					nochged->s[k] |= 4;
					n += set_pos_sim( hap1, hap2, nochged, k);
				}  //end of while(n < HD_NUM)				
				break;
			case 1: 
				n = 0; 
				while(n < highp->s[i].num){ //set mutation in High density area
					k = highp->s[i].beg - abszzm( zt_stdfun() )*highp->s[i].dalt; // Left side normal distribution in High density area
					c0 = ret[0]->s[k];
					c1 = ret[1]->s[k];
					while (c0 >= 4 || c1 >= 4 || nochged->s[k]){
						k = highp->s[i].beg - abszzm( zt_stdfun() )*highp->s[i].dalt;
						c0 = ret[0]->s[k];
						c1 = ret[1]->s[k];
					}	
					nochged->s[k] |= 4;
					n += set_pos_sim( hap1, hap2, nochged, k);
				}  //end of while(n < HD_NUM)
				break;				
			case 2:					
				n = 0; 
				while(n < highp->s[i].num){ //set mutation in High density area
					k = highp->s[i].beg + abszzm( zt_stdfun() )*highp->s[i].dalt; // Right side normal distribution in High density area
					c0 = ret[0]->s[k];
					c1 = ret[1]->s[k];
					while (c0 >= 4 || c1 >= 4 || nochged->s[k]){
						k = highp->s[i].beg + abszzm( zt_stdfun() )*highp->s[i].dalt;
						c0 = ret[0]->s[k];
						c1 = ret[1]->s[k];
					}	
					nochged->s[k] |= 4;
					n += set_pos_sim( hap1, hap2, nochged, k);
				}  //end of while(n < HD_NUM)
				break;
			case 3:					
				n = 0; 
				while(n < highp->s[i].num){ //set mutation in High density area
					k = highp->s[i].beg + zt_stdfun()*highp->s[i].dalt; // left and right normal distribution in High density area
					c0 = ret[0]->s[k];
					c1 = ret[1]->s[k];
					while (c0 >= 4 || c1 >= 4 || nochged->s[k]){
						k = highp->s[i].beg + zt_stdfun()*highp->s[i].dalt;
						c0 = ret[0]->s[k];
						c1 = ret[1]->s[k];
					}	
					nochged->s[k] |= 4;
					n += set_pos_sim( hap1, hap2, nochged, k);
				}  //end of while(n < HD_NUM)
				break;
			default: break;
			}
		} //for(i=0; i<highp->l; i++) 
	} 

	num = (int)(ret[0]->v * LOH_NOR_AB_RATE );
	printf("\tEnter LOH_NOR_AB_RATE set:  LOH_NOR_AB num=%d   nor_AB_tab size=%d\n", num, norab->l);
	for (j=0; j<num; j++){  // mutation from normal AB mode 
		k = zrand()*norab->l;
		n = norab->s[k];
		c0 = ret[0]->s[n];
		c1 = ret[1]->s[n];
		m = 0;
		while (m<10 && ( (c0&0x30) > GTYPEAB || (c1&0x30) > GTYPEAB || nochged->s[n] ) ){

			k = zrand()*norab->l;
			n = norab->s[k];
			c0 = ret[0]->s[n];
			c1 = ret[1]->s[n];
			m++;
		}
		if (m<10){
			c0 = ret[0]->s[n]; 
			c1 = ret[1]->s[n];
			if (zrand() < LOH_BB_RATE) { //AB==>BB
				if ((c0 & 0x30) == GTYPEAB){ 
					ret[0]->s[n] |= SUBSTITUTE|GTYPELOH;
					ret[1]->s[n] |= DELETE|GTYPEAB;
				} else {
					ret[1]->s[n] |= SUBSTITUTE|GTYPELOH;
					ret[0]->s[n] |= DELETE|GTYPEAB; 
				}
			}else{  //AB==>AA
				if ((c0 & 0x30) == GTYPEAB){
					ret[1]->s[n] |= SUBSTITUTE|GTYPELOH;
					ret[0]->s[n] |= DELETE|GTYPEAB;
				} else {
					ret[0]->s[n] |= SUBSTITUTE|GTYPELOH;
					ret[1]->s[n] |= DELETE|GTYPEAB; 
				}
			}
			nochged->s[n] |= 4;
		} else printf("\tFailed to Search NOR_AB_TAB:\n\t\thas seted num=%d    try num = %d   rand tab pos=%d   reference pos=%d\n", j, m, k, n);
	}
	printf("\tend LOH_NOR_AB_RATE set:  has set LOH_NOR_AB num=%d\n", j);
	return;
}


void read_high_tab( high_t *highp)
{
	int pos, n, idno, idbeg, idlen, idmark, idnum, iddalt;

	// read base *.sim.idx for long indel 

	if (!highfp || !HIGH_DENSITY) return;   // No High Density Area ZT set !
	n=0;
	while ( fscanf(highfp, "%d %d %d %d %d %d\n", &idno, &idbeg, &idlen, &idmark, &idnum, &iddalt) != EOF ){
		pos = highp->l;
		if (pos+1 >= highp->max){
			highp->max += SEQ_BLOCK_SIZE;
			highp->s = (high_e *)realloc(highp->s, sizeof(high_e) * highp->max);
		}
		highp->s[pos].no =idno;
		highp->s[pos].beg=idbeg;
		highp->s[pos].len=idlen;
		highp->s[pos].mark=idmark;
		highp->s[pos].num=idnum;
		highp->s[pos].dalt=iddalt; 
		highp->l++;
		n++;
	} //while (reading file .....)
	printf("read end of High density num=%d \n", n);
	fclose(highfp);
}

/*******************************************************/
int addinsert_iterm(indel_t *indelp, int idno, int idbeg, int idlen, int idtype, int idnum, int idvar, char *idstr, int *n2){
int pos;
	pos = indelp->l;
	if (pos+1 >= indelp->max){
		indelp->max += SEQ_BLOCK_SIZE;
		indelp->s = (indel_e *)realloc(indelp->s, sizeof(indel_e) * indelp->max);
	}
	indelp->s[pos].indel_no =idno;
	indelp->s[pos].indel_beg=idbeg;
	indelp->s[pos].indel_len=idlen;
	indelp->s[pos].indel_type=idtype;
	indelp->s[pos].indel_num=idnum;
	indelp->s[pos].indel_var=idvar;
	indelp->s[pos].str = (char *)calloc(strlen(idstr)+1, sizeof(char));
	strcpy(indelp->s[pos].str, idstr);
	indelp->l++;
	*n2 = *n2 + idlen;
	return 0;
}



/*******************************************************/
void init_del_sim( seq_t *seq, mutseq_t *hap1, mutseq_t *hap2, seq_t *nochged, mutseq_t *norab , indel_t *indelp, high_t *highp, char *simfile)
{
	int fret, no, idno, idbeg, idlen, idtype, idnum, idvar;
	char ch=0, name[50], *idstr, *idmark, base_simidxf[50]; //[100000];
	int i,j,k, n1, n2, h, del_no, in_no, addnum;
	mutseq_t *ret[2];
	int tmp, n, pos;
	double r;
	mut_t s0, s1, indelmk;
	FILE *inbasesimfp;

	ret[0] = hap1; ret[1] = hap2;
	ret[0]->v = 0; ret[1]->v = 0;
	ret[0]->l = seq->l; ret[1]->l = seq->l;
	ret[0]->max = seq->max; ret[1]->max = seq->max;
	ret[0]->s = (mut_t *)calloc(seq->max, sizeof(mut_t));
	ret[1]->s = (mut_t *)calloc(seq->max, sizeof(mut_t));
	norab->l = seq->l; norab->max = seq->max;
	norab->s = (mut_t *)calloc(seq->max, sizeof(mut_t));
	nochged->l = seq->l; nochged->max = seq->max;
	nochged->s = (char *)calloc(seq->max, sizeof(char));

	n=0;
	for (i = 0; i < seq->l; ++i) {
		ret[0]->s[i] = ret[1]->s[i] = (mut_t)gch_to_num(seq->s[i]);
		norab->s[i] = 0;
		nochged->s[i] = 0;
		if ( (ret[0]->s[i]&0xf) < 4) n++;
	}
	ret[0]->v = ret[1]->v = n;

	// read base *.sim file
	n=0;
	fret=fscanf(insimfp, "%s\n", name);
	fret=fscanf(insimfp, "%d %u %u\n", &pos, &s0, &s1);
	while (fret != EOF){
		n++;
		ret[0]->s[pos] = s0;
		ret[1]->s[pos] = s1;
		fret=fscanf(insimfp, "%d %u %u\n", &pos, &s0, &s1);
	}
	printf("\tread base *.sim file.  Num = %d\n", n);

	////record AB mode in normoal reference
	n=0;
	while (fscanf(innorabfp, "%d %u %u\n", &pos, &s0, &s1) != EOF){
		s0 = ret[0]->s[pos];
		s1 = ret[1]->s[pos];
		if ( (s0 & 0x30) == GTYPEAB || (s1 & 0x30) == GTYPEAB ) {
			norab->s[n] = pos; 
			n++; 
		}
	}
	norab->l = n;
	printf("\tread AB mode in normoal reference.  Num = %d\n", n);

	////set forbid other turmor changed position
	for (i=0; i<OTHER_CHGED; i++){
		n=0;
		inochgfp = FileOpen( INOCHGFs[i],  "r"); 
		//fret=fscanf(inochgfp, "%s\n", name);
		fret=fscanf(inochgfp, "%d %u %u\n", &pos, &s0, &s1);
		while (fret != EOF){
			n++;
			if ( s0 & 0xc0 ) nochged->s[pos] |= 1;
			if ( s1 & 0xc0 ) nochged->s[pos] |= 2;
			fret=fscanf(inochgfp, "%d %u %u\n", &pos, &s0, &s1);
		}
		fclose(inochgfp); 
		printf("\tset forbid other turmor changed position from %s.  Num = %d\n", INOCHGFs[i], n);
	}
	
	// read high density ZT set file
	read_high_tab( highp);

	// read base *.sim.idx for long indel 
	if (!indelfp || !LONG_INDEL)return;   // No Long Insert/Delete and Repeating !

	idstr = new char [100000];
	idmark = new char [100000];
	sprintf(base_simidxf, "%s.idx", simfile);
	inbasesimfp = FileOpen( base_simidxf,  "r"); 
	if (inbasesimfp){
		n=0; n2 =0;
		while ( fscanf(inbasesimfp, "%d %d %d %d %d %d %s\n", &idno, &idbeg, &idlen, &idtype, &idnum, &idvar, idstr) != EOF ){
			addnum=idno;
			addinsert_iterm(indelp, idno, idbeg, idlen, idtype, idnum, idvar, idstr, &n2);
			n++;
		} //while (reading file .....)
		printf("read end of base *.sim.idx:: Long insert num=%d    long insert len=%d    Long insert table size=%d\n", n, n2, indelp->l);
		fclose(inbasesimfp);
	}

	// handle for long delete area from long indel file
	n1=0; n2 = 0; del_no = 0;in_no=0; 
	addnum++;
	while ( fscanf(indelfp, "%d %d %d %d %d %d %s\n", &idno, &idbeg, &idlen, &idtype, &idnum, &idvar, idstr) != EOF ){
		if (idlen<0) {  // long delete from long indel file!!!
			idlen=-idlen;
			if (idbeg<0 || idbeg+idlen-1>=REF_LEN) idtype=-1;
			switch(idtype){
			case 0: break;
			case 1: h = 0;
					for (int i=0; i<idlen; i++){
						indelmk = ret[h]->s[idbeg+i]& LONGINSERT;
						if (indelmk == LONGINSERT)ret[h]->s[idbeg+i] = (ret[h]->s[idbeg+i] & NOTDELETE )|GTYPEAB;
						else ret[h]->s[idbeg+i] |= DELETE|GTYPEAB;
					}
					n1 = n1 + idlen;
					del_no ++;
					break;
			case 2: h = 1;
					for (int i=0; i<idlen; i++){
						indelmk = ret[h]->s[idbeg+i]& LONGINSERT;
						if (indelmk == LONGINSERT)ret[h]->s[idbeg+i] = (ret[h]->s[idbeg+i] & NOTDELETE )|GTYPEAB;
						else ret[h]->s[idbeg+i] |= DELETE|GTYPEAB;
					}
					n1 = n1 + idlen;
					del_no ++;
					break;			
			case 3:	for (int i=0; i<idlen; i++){
						h = 0;
						indelmk = ret[h]->s[idbeg+i]& LONGINSERT;
						if (indelmk == LONGINSERT)ret[h]->s[idbeg+i] = (ret[h]->s[idbeg+i] & NOTDELETE )|GTYPEBB;
						else ret[h]->s[idbeg+i] |= DELETE|GTYPEBB;
						h = 1;
						indelmk = ret[h]->s[idbeg+i]& LONGINSERT;
						if (indelmk == LONGINSERT)ret[h]->s[idbeg+i] = (ret[h]->s[idbeg+i] & NOTDELETE )|GTYPEBB;
						else ret[h]->s[idbeg+i] |= DELETE|GTYPEBB;
					}
					n1 = n1 + idlen;
					del_no ++;
					break;
			default: break;
			}
			if (idtype == -1) printf("\tInvalid DEL  No= %d   beg: %d    len: %d\n", del_no, idbeg, idlen);
		}else {  //long Insert from long indel file
			if (idbeg<0 || (idbeg+idlen>=REF_LEN) || idnum<1) idtype=-1;
			switch(idtype){
			case 0: break;
			case 1: h = 0;
				indelmk = ret[h]->s[idbeg]& DELETE;
				ret[h]->s[idbeg] = (ret[h]->s[idbeg] & 0x03 );
				if (indelmk == DELETE) ret[h]->s[idbeg] |= (LONGINSERT|GTYPEAB|(addnum<<8));
				else ret[h]->s[idbeg] |= (LONGINSERT|INSERT|GTYPEAB|(addnum<<8));
				break;
			case 2: h = 1;
				indelmk = ret[h]->s[idbeg]& DELETE;
				ret[h]->s[idbeg] = (ret[h]->s[idbeg] & 0x03 );
				if (indelmk == DELETE) ret[h]->s[idbeg] |= (LONGINSERT|GTYPEAB|(addnum<<8));
				else ret[h]->s[idbeg] |= (LONGINSERT|INSERT|GTYPEAB|(addnum<<8));
				break;
			case 3: h = 0;
				indelmk = ret[h]->s[idbeg]& DELETE;
				ret[h]->s[idbeg] = (ret[h]->s[idbeg] & 0x03 );
				if (indelmk == DELETE) ret[h]->s[idbeg] |= (LONGINSERT|GTYPEBB|(addnum<<8));
				else ret[h]->s[idbeg] |= (LONGINSERT|INSERT|GTYPEBB|(addnum<<8));
				h = 1;
				indelmk = ret[h]->s[idbeg]& DELETE;
				ret[h]->s[idbeg] = (ret[h]->s[idbeg] & 0x03 );
				if (indelmk == DELETE) ret[h]->s[idbeg] |= (LONGINSERT|GTYPEBB|(addnum<<8));
				else ret[h]->s[idbeg] |= (LONGINSERT|INSERT|GTYPEBB|(addnum<<8));
				break;
			default: break;
			}
			if (idtype > 0) {
				int res_num, curr_num;
				if (idvar == 0){  //repeat without var genos
					for(i=0; i<idnum; i++) addinsert_iterm(indelp, addnum, idbeg, idlen, idtype, idnum, idvar, idstr, &n2);
				}else { //repeat with var genos
					for(i=0; i<strlen(idstr); i++) idmark[i] = 0;
					res_num = idnum;
					while(res_num>4){
						k = zrand()*(0.5*res_num-1)+1;
						for(i=0; i<k; i++) addinsert_iterm(indelp, addnum, idbeg, idlen, idtype, idnum, idvar, idstr, &n2);
						res_num = res_num - k;
						j = zrand()*strlen(idstr);
						while (idmark[j]) j = zrand()*strlen(idstr);
						idmark[j] = 1;
						i = (gch_to_num(idstr[j]) + (int)(zrand()* 3.0 + 1)) & 3;
						idstr[j] = num_to_gch(i);
					}
					if (res_num>1){
						k = zrand()*(res_num-1)+1;
						for(i=0; i<k; i++) addinsert_iterm(indelp, addnum, idbeg, idlen, idtype, idnum, idvar, idstr, &n2);
						res_num = res_num - k;
						j = zrand()*strlen(idstr);
						while (idmark[j]) j = zrand()*strlen(idstr);
						idmark[j] = 1;
						i = (gch_to_num(idstr[j]) + (int)(zrand()* 3.0 + 1)) & 3;
						idstr[j] = num_to_gch(i);
						for(i=0; i<res_num; i++) addinsert_iterm(indelp, addnum, idbeg, idlen, idtype, idnum, idvar, idstr, &n2);
					}else if (res_num == 1) addinsert_iterm(indelp, addnum, idbeg, idlen, idtype, idnum, idvar, idstr, &n2);
				}//if(ifvar ==0 )...
				in_no++;
				addnum++;
			}else printf("\tInvalid INSERT  No= %d   beg: %d    len: %d\n", in_no, idbeg, idlen);
		}//if...else...
	} //while (reading file .....)
	printf("\tEND:: Long del_no=%d  del_len=%d;  Long insert num=%d  in_lens=%d;   Long insert table size=%d\n", del_no, n1, in_no, n2, indelp->l);
	delete [] idstr;
	delete [] idmark;
	return;
}

/**********************************************/
void print_simfile(mutseq_t *hap1, mutseq_t *hap2, char *refname, seq_t *nochged, indel_t *indelp)
{
	int i, n, n0,n1,cnt0,cnt1, chg_no;

	n=n0=n1=cnt0=cnt1=0; chg_no=0;
	fprintf(outsimfp,"&%s\n", refname);
	for (i = 0; i < hap1->l; ++i) {
		if ((hap1->s[i]&mutmsk)!=DELETE && (hap1->s[i] & 0xf) < 4 ){
			n0++;
			if ((hap1->s[i] & mutmsk) == INSERT ) n0=n0+(hap1->s[i]>>28);
		}
		if ((hap2->s[i] & mutmsk)!=DELETE && (hap2->s[i] & 0xf) < 4 ){
			n1++;
			if ((hap2->s[i] & mutmsk) == INSERT ) n1=n1+(hap2->s[i]>>28);
		}
		if( nochged->s[i] & 4 ){
			if (OUT_CHGED) fprintf(outchgfp, "%10u\t%10u\t%10u\n", i, hap1->s[i], hap2->s[i]);
			chg_no++;
		}

		if ((hap1->s[i] & mutmsk) || (hap2->s[i] & mutmsk) ) {
			n++;
			if (hap1->s[i] & mutmsk) cnt0++;
			if (hap2->s[i] & mutmsk) cnt1++;
			fprintf(outsimfp, "%10u\t%10u\t%10u\n", i, hap1->s[i], hap2->s[i]);
		}
		if ((hap1->s[i] & mutmsk)==SUBSTITUTE || (hap2->s[i] & mutmsk)==SUBSTITUTE ) {
			if (((hap1->s[i] & 0x30)==GTYPEAB)|| ((hap2->s[i] & 0x30)==GTYPEAB))
			fprintf(outsABp, "%10u\t%10u\t%10u\n", i, hap1->s[i], hap2->s[i]);
			if (((hap1->s[i] & 0x30)==GTYPELOH)|| ((hap2->s[i] & 0x30)==GTYPELOH))
			fprintf(outLOHp, "%10u\t%10u\t%10u\n", i, hap1->s[i], hap2->s[i]);
		}
	}
	// out long insert TABLE from long indel file
	if (indelfp && LONG_INDEL && indelp->l){
		for (i = 0; i < indelp->l; ++i) {
			fprintf(outindelfp, "%d %d %d %d %d %d %s\n", indelp->s[i].indel_no, indelp->s[i].indel_beg, 
					indelp->s[i].indel_len, indelp->s[i].indel_type, indelp->s[i].indel_num, indelp->s[i].indel_var, indelp->s[i].str);
		}
	}
	if (n>0){
		printf("\tref1_len=%d  ref1 var num = %d Var rate = %.10f\n\n",n0,cnt0,1.0*cnt0/n0);
		printf("\tref2_len=%d  ref2 var num = %d Var rate = %.10f\n\n",n1,cnt1,1.0*cnt1/n1);
		printf("\tRef_len =%d  all  var num = %d Var rate = %.10f chged_gch num= %d\n\n", REF_LEN,n,1.0*n/REF_LEN, chg_no);
		fprintf(outresult,"\n ref1_len=%d  ref1 var num = %d Var rate = %.10f\n\n",n0,cnt0,1.0*cnt0/n0);
		fprintf(outresult,"\n ref2_len=%d  ref2 var num = %d Var rate = %.10f\n\n",n1,cnt1,1.0*cnt1/n1);
		fprintf(outresult,"\n Ref_len =%d  all  var num = %d Var rate = %.10f chged_files = %d\n\n",
					REF_LEN,n,1.0*n/REF_LEN, chg_no);
	}
	return;
}

/**********************************************/
#define PACKAGE_VERSION "1.0.1"
static int simu_usage()
{
	printf("\n\nProgram: TumSim (the Tumor Simulator for the data generator)\n");
	printf("Version: %s \n", PACKAGE_VERSION);
	printf("Contact: Yu Geng <gengyu@stu.xjtu.edu.cn>;Zhongmeng Zhao <zmzhao@mail.xjtu.edu.cn>\n");
	printf("Usage:   tumsim [options] <ref.fa> <base.sim> <nor_AB.idx> <subclone.sim>\n");
	printf("Options: -r FLOAT      	mutation rate of SV [%12.10f]\n", SV_MUT_RATE); //[0.001]
	printf("         -R FLOAT      	fraction of indels [%10.6f]\n",INDEL_FRAC); //0.1
	printf("         -X FLOAT      	probability an indel is extended [%10.6f]\n",INDEL_EXTEND);//0.3
	printf("         -D FLOAT      	delete rate in indel [%10.6f]\n", DEL_RATE);//0.5
	printf("         -B FLOAT      	BB rate in mutation [%10.6f]\n", BB_RATE);//0.33333
	printf("         -b FLOAT      	LOH BB rate in mutation [%10.6f]\n", LOH_BB_RATE);//0.5
	printf("         -A FLOAT      	mutation rate of LOH in normal AB [%12.10f]\n", LOH_NOR_AB_RATE);//0.001
	printf("         -H <highdensity.txt>  	input high density set file. \n");
	printf("         -I <indelpos.txt>  	input long indel set file. generating <subclone.sim.idx> for 'ReadGen'\n");
	printf("         -N <other_chged.idx> 	input changed positions in other subclone(may multi-choices)\n");
	printf("         -C <*_chged.idx>  		output changed positions in this turmor\n");
	printf("         -o <*_simout.txt>  	output result for runing case\n");
	return 1;
}


/**********************************************/
int main(int argc, char *argv[])
{	char c, cc, fhead[50];
	int i;
	char ingenf[50];
	char insimf[50];
	char innorabf[50];
	char indelf[50];
	char highf[50];
	char outindelf[50];
	char outchgf[50];
	char outsimf[50];
	char outresultf[50];
	char outsABf[50];
	char outLOHf[50];
	seq_t	 seq;
	mutseq_t sim[2], norab;
	seq_t    nochged;
	char ChrName[50];
	indel_t  indelp;
	high_t  highp;

	srand((int)time(0));  // Init Random data 
	outresultf[0]='\0';
	indelf[0]='\0';
	cc=0;
	while ((c = getopt(argc, argv, "r:R:X:D:B:b:A:H:I:N:C:o:")) >= 0) {
		switch (c) {
		case 'r': SV_MUT_RATE     = atof(optarg); if (SV_MUT_RATE<0 || SV_MUT_RATE>1) cc=1; break;
		case 'R': INDEL_FRAC   = atof(optarg); if (INDEL_FRAC<0 || INDEL_FRAC>1) cc=1; break;
		case 'X': INDEL_EXTEND = atof(optarg); if (INDEL_EXTEND<0 || INDEL_EXTEND>1) cc =1; break;
		case 'D': DEL_RATE = atof(optarg);     if (DEL_RATE<0 || DEL_RATE>1) cc =1; break;
		case 'B': BB_RATE = atof(optarg);      if (BB_RATE<0 || BB_RATE>1) cc =1; break;
		case 'b': LOH_BB_RATE = atof(optarg);      if (LOH_BB_RATE<0 || LOH_BB_RATE>1) cc =1; break;
		case 'A': LOH_NOR_AB_RATE = atof(optarg);  if (LOH_NOR_AB_RATE<0 || LOH_NOR_AB_RATE>1) cc =1; break;
		case 'H': HIGH_DENSITY = 1; strcpy(highf,optarg); break;
		case 'I': LONG_INDEL = 1; strcpy(indelf, optarg); break;
		case 'N': strcpy(INOCHGFs[OTHER_CHGED], optarg); OTHER_CHGED++; break;
		case 'C': OUT_CHGED = 1; strcpy(outchgf, optarg);break;
		case 'o': strcpy(outresultf, optarg);break;
		default : break;
		}
		if (cc) {optind = argc; break;}
	}
	if (optind != (argc-4) ) return simu_usage();
	strcpy(ingenf, argv[optind+0]);
	strcpy(insimf, argv[optind+1]);
	strcpy(innorabf, argv[optind+2]);
	strcpy(outsimf, argv[optind+3]);

	for (i=0; i<strlen(outsimf); i++) if (outsimf[i] == '.') break; 
	if (i<strlen(outsimf) ) { strncpy(fhead, &outsimf[0], i); fhead[i]='\0'; }
	else strcpy(fhead, outsimf); 
	if (strlen(outresultf)==0) sprintf(outresultf, "%s_simout.txt", fhead); 
	sprintf(outsABf,"%s_AB.idx",fhead);
	sprintf(outLOHf,"%s_LOH.sim",fhead);

	if (LONG_INDEL){
		indelfp = FileOpen( indelf,  "r"); 
		sprintf(outindelf, "%s.idx", outsimf);
		outindelfp = FileOpen( outindelf,  "w"); 
	}else indelfp = NULL;
	if (HIGH_DENSITY){
		highfp = FileOpen( highf,  "r"); 
	}else indelfp = NULL;
	if (OUT_CHGED) outchgfp = FileOpen( outchgf,  "w"); else outchgfp = NULL;
	ingenfp  = FileOpen( ingenf,  "r"); 
	insimfp  = FileOpen( insimf,  "r"); 
	innorabfp  = FileOpen( innorabf,  "r"); 
	outsimfp = FileOpen( outsimf,   "w");
	outresult = FileOpen( outresultf, "w");
	outsABp = FileOpen(outsABf,"w");
	outLOHp = FileOpen(outLOHf,"w");

	INIT_SEQ(seq);
	INIT_SEQ(norab);
	INIT_SEQ(nochged);
	INIT_SEQ(sim[0]);
	INIT_SEQ(sim[1]);
	INIT_SEQ(indelp);
	INIT_SEQ(highp);

	//* read reference fasta */	
	REF_LEN = seq_read_fasta(ingenfp, &seq, ChrName, 0);
	printf("seq_read_fasta() return! REF_LEN = %d\n\n",REF_LEN);

	//* init sim and set long DEL */ 
	init_del_sim(&seq, &sim[0], &sim[1], &nochged, &norab, &indelp, &highp, insimf );
	printf("init_del_sim() return!\n\n");

	//* set mutation for simsulator */
	set_mut_sim(&seq, &sim[0], &sim[1], &nochged, &norab, &highp );
	printf("set_mut_sim() return!\n\n");

	//out put simulator file
	print_simfile( &sim[0], &sim[1], ChrName, &nochged, &indelp);
	printf("print_simfile() return!\n\n");

	CLEAR_SEQ(seq);
	CLEAR_SEQ(norab);
	CLEAR_SEQ(nochged);
	CLEAR_MUTSEQ(sim[0]);
	CLEAR_MUTSEQ(sim[1]);
	for (i=0; i<indelp.l; i++) if (indelp.s[i].str) free(indelp.s[i].str);
	CLEAR_SEQ(indelp);
	CLEAR_SEQ(highp);

	if(indelfp){ fclose(indelfp); fclose(outindelfp);} 
	//if(highfp) fclose(highfp);
	if(outchgfp) fclose(outchgfp);
	fclose(ingenfp); 
	fclose(insimfp); 
	fclose(innorabfp); 
 	fclose(outsimfp);
	fclose(outresult);
	fclose(outsABp);
	fclose(outLOHp);
	return 0;
}  // end of main()
