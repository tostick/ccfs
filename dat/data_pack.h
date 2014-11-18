#ifndef _DATA_PACK_H_
#define _DATA_PACK_H_

#define PUT64BIT(var,ptr) {                                           \
	(ptr)[0]=((var)>>56)&0xFF;                                        \
	(ptr)[1]=((var)>>48)&0xFF;                                        \
	(ptr)[2]=((var)>>40)&0xFF;                                        \
	(ptr)[3]=((var)>>32)&0xFF;                                        \
	(ptr)[4]=((var)>>24)&0xFF;                                        \
	(ptr)[5]=((var)>>16)&0xFF;                                        \
	(ptr)[6]=((var)>>8)&0xFF;                                         \
	(ptr)[7]=(var)&0xFF;                                              \
	(ptr)+=8;                                                         \
}

#define PUT32BIT(var,ptr) {                                           \
	(ptr)[0]=((var)>>24)&0xFF;                                        \
	(ptr)[1]=((var)>>16)&0xFF;                                        \
	(ptr)[2]=((var)>>8)&0xFF;                                         \
	(ptr)[3]=(var)&0xFF;                                              \
	(ptr)+=4;                                                         \
}


#define PUT16BIT(var,ptr) {                                           \
	(ptr)[0]=((var)>>8)&0xFF;                                         \
	(ptr)[1]=(var)&0xFF;                                              \
	(ptr)+=2;                                                         \
}

#define PUT8BIT(var,ptr) {                                            \
	*(ptr)=(var)&0xFF;                                                \
	(ptr)++;                                                          \
}

#define GET64BIT(var,ptr) {                                           \
	(var)=((ptr)[3]+256*((ptr)[2]+256*((ptr)[1]+256*(ptr)[0])));      \
	(var)<<=32;                                                       \
	(var)|=(((ptr)[7]+256*((ptr)[6]+256*((ptr)[5]+256*(ptr)[4]))))&0xffffffff;  \
	(ptr)+=8;                                                         \
}

#define GET32BIT(var,ptr) {                                           \
	(var)=((ptr)[3]+256*((ptr)[2]+256*((ptr)[1]+256*(ptr)[0])));      \
	(ptr)+=4;                                                         \
}


#define GET16BIT(var,ptr) {                                           \
	(var)=(ptr)[1]+256*(ptr)[0];                                      \
	(ptr)+=2;                                                         \
}

#define GET8BIT(var,ptr) {                                            \
	(var)=*(ptr);                                                     \
	(ptr)++;                                                          \
}

#endif
