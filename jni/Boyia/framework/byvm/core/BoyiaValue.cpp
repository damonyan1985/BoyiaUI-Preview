//
// Created by yanbo on 2017/9/26.
//

#include "PlatformLib.h"
#include "BoyiaMemory.h"
#include "AutoLock.h"
#include "MiniMutex.h"
#include "JSBase.h"
#include "SalLog.h"
#include <stdlib.h>
#include <stdio.h>
#include <android/log.h>

#define MAX_INT_LEN 20
#define MINI_ANDROID_LOG
#define MEMORY_SIZE           (LInt)1024*1024*6
static BoyiaMemoryPool*        gMemPool = NULL;

extern LVoid GCAppendRef(LVoid* address, LUint8 type);
extern LVoid GCollectGarbage();

extern void jsLog(const char* format, ...) {
	va_list args;
	va_start(args, format);
#ifdef MINI_ANDROID_LOG
	__android_log_vprint(ANDROID_LOG_INFO, "BoyiaVM", format, args);
#else
    printf(format, args);
#endif
    va_end(args);
}

extern LInt Str2Int(LInt8* p, LInt len, LInt radix) {
    //LUint8 *p = (LUint8*) ptr;
    LInt total = 0;
    LInt sign = 1;
    LInt pos = 0;

    if (*p == '-') {
        sign = -1;
        ++pos;
    } else if (*p == '+') {
    	++pos;
    }

    while (pos < len) {
        LInt ch = 0;
        if (LIsDigit(*(p + pos))) {
            ch = *(p + pos) - '0';
        } else if (LIsBigChar(*(p + pos))) {
            ch = *(p + pos) - 'A' + 10;
        } else if (LIsMinChar(*(p + pos))) {
            ch = *(p + pos) - 'a' + 10;
        }

        total = total * radix + ch;
        ++pos;
    }

    return total * sign;
}

// 对MJS属性进行插入排序，方便二分查找
extern LVoid MiniSort(BoyiaValue* vT, LInt len) {
	for (LInt i = 1; i<len; ++i) {
		for (LInt j = i; j - 1 >= 0 && vT[j].mNameKey<vT[j - 1].mNameKey; --j) {
			BoyiaValue tmp;
			ValueCopy(&tmp, &vT[j]);
			ValueCopy(&vT[j], &vT[j - 1]);
			ValueCopy(&vT[j - 1], &tmp);
		}
	}
}

// 主要作用用于优化MJS对象属性HashCode查找
// 对属性Props数组进行二分查找
LInt MiniSearch(BoyiaValue*  vT, LInt n, LUint key) {
	LInt low = 0, high = n - 1, mid;

	while (low <= high) {
		mid = (low + high) / 2;
		if (key < vT[mid].mNameKey) {
			high = mid - 1;
		} else if (key > vT[mid].mNameKey) {
			low = mid + 1;
		} else {
			return mid;
		}
	}

	return -1;
}

LVoid CreateMiniMemory() {
	gMemPool = initMemoryPool(MEMORY_SIZE);
}

LVoid* MiniNew(LInt size) {
	LVoid* data = newData(size, gMemPool);
	__android_log_print(ANDROID_LOG_INFO, "MiniJS", "MiniJS POOL addrmax=%x ptr=%x long size=%d", (LInt)gMemPool->m_address + gMemPool->m_size, (LInt)data, sizeof(long));
	printPoolSize(gMemPool);
	return data;
	//return malloc(size);
}

LVoid MiniDelete(LVoid* data) {
    deleteData(data, gMemPool);
	//free(data);
}

LVoid MStrcpy(BoyiaStr* dest, BoyiaStr* src) {
    dest->mPtr = src->mPtr;
    dest->mLen = src->mLen;
}

//LUint HashCode(BoyiaStr* str) {
//	return GenHashCode(str->mPtr, str->mLen);
//}

LVoid InitStr(BoyiaStr* str, LInt8* ptr) {
    str->mLen = 0;
    str->mPtr = ptr;
}

LBool MStrchr(const LInt8 *s, LInt8 ch) {
    while (*s && *s != ch) ++s;
    return *s && *s == ch;
}

LBool MStrcmp(BoyiaStr* src, BoyiaStr* dest) {
    if (src->mLen != dest->mLen) {
        return LFalse;
    }

    // 地址一样直接返回true
    if (src->mPtr == dest->mPtr) {
        return LTrue;
    }

    LInt len = src->mLen;
    while (len--) {
        if (*(src->mPtr + len) != *(dest->mPtr + len)) {
            return LFalse;
        }
    }

    return LTrue;
}

extern LVoid NativeDelete(LVoid* data) {
	delete (boyia::JSBase*) data;
}

extern LVoid SystemGC() {
    if (gMemPool->m_used >= (MEMORY_SIZE/2)) {
    	GCollectGarbage();
    }
}

// "Hello" + "World"
static LVoid FetchString(BoyiaStr* str, BoyiaValue* value) {
	if (value->mValueType == INT) {
		str->mPtr = NEW_ARRAY(LInt8, MAX_INT_LEN);
		LMemset(str->mPtr, 0, MAX_INT_LEN);
		LInt2StrWithLength(value->mValue.mIntVal, (LUint8*)str->mPtr, 10, &str->mLen);
	} else {
		str->mPtr = value->mValue.mStrVal.mPtr;
		str->mLen = value->mValue.mStrVal.mLen;
	}
}

extern LVoid StringAdd(BoyiaValue* left, BoyiaValue* right) {
	KLOG("StringAdd Begin");
	BoyiaStr leftStr, rightStr;
	LInt8 tmpArray[MAX_INT_LEN];
	leftStr.mPtr = tmpArray;
	rightStr.mPtr = tmpArray;
	FetchString(&leftStr, left);
	FetchString(&rightStr, right);

	LInt len = leftStr.mLen + rightStr.mLen;
	LInt8* str = NEW_ARRAY(LInt8, len);

	LMemcpy(str, leftStr.mPtr, leftStr.mLen);
	LMemcpy(str + leftStr.mLen, rightStr.mPtr, rightStr.mLen);
	right->mValue.mStrVal.mPtr = str;
	right->mValue.mStrVal.mLen = len;
	right->mValueType = STRING;

	GCAppendRef(str, STRING);
	KLOG("StringAdd End");
}

typedef struct MiniId {
	BoyiaStr mStr;
	LUint   mID;
	MiniId* mNext;
} MiniId;

typedef struct {
	MiniId* mBegin;
	MiniId* mEnd;
} MiniIdLink;

static MiniIdLink* sIdLink = NULL;
static LUint sIdCount = 0;
LUint GenIdentByStr(const LInt8* str, LInt len) {
	BoyiaStr strId;
	strId.mPtr = (LInt8*)str;
	strId.mLen = len;
	return GenIdentifier(&strId);
}

LUint GenIdentifier(BoyiaStr* str) {
    if (!sIdLink) {
    	sIdLink = new MiniIdLink;
    	sIdLink->mBegin = NULL;
    	sIdLink->mEnd = NULL;
    }

    MiniId* id = sIdLink->mBegin;
    while (id) {
        if (MStrcmp(str, &id->mStr)) {
        	return id->mID;
        }

        id = id->mNext;
    }

    id = new MiniId;
    id->mID = ++sIdCount;
    id->mStr.mPtr = new LInt8[str->mLen];
    id->mStr.mLen = str->mLen;
    LMemcpy(id->mStr.mPtr, str->mPtr, str->mLen);
    id->mNext = NULL;

    if (!sIdLink->mBegin) {
    	sIdLink->mBegin = id;
    	sIdLink->mEnd = id;
    } else {
        sIdLink->mEnd->mNext = id;
        sIdLink->mEnd = id;
    }

    return id->mID;
}