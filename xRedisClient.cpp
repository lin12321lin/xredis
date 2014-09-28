/*
 * ----------------------------------------------------------------------------
 * Copyright (c) 2013-2014, xSky <guozhw at gmail dot com>
 * All rights reserved.
 * Distributed under GPL license.
 * ----------------------------------------------------------------------------
 */
 
#include "xRedisClient.h"
#include "xRedisPool.h"
#include <sstream>


RedisDBIdx::RedisDBIdx() {
    mType = 0;
    mIndex = 0;
    mStrerr = NULL;
    mClient = NULL;
}

RedisDBIdx::RedisDBIdx(xRedisClient *xredisclient) {
    mType = 0;
    mIndex = 0;
    mStrerr = NULL;
    mClient = xredisclient;
}
RedisDBIdx::~RedisDBIdx() {
    if (NULL != mStrerr){
        delete[] mStrerr;
        mStrerr = NULL;
    }
}

bool RedisDBIdx::CreateDBIndex(const char *key,  HASHFUN fun, const unsigned int type) {
    unsigned int hashbase = mClient->GetRedisPool()->getHashBase(type);
    if ((NULL!=fun) && (hashbase>0)) {
        mIndex = fun(key)%hashbase;
        mType  = type;
        return true;
    }
    return false;
}

bool RedisDBIdx::CreateDBIndex(const int64_t id, const unsigned int type) {
    unsigned int hashbase = mClient->GetRedisPool()->getHashBase(type);
    if (hashbase>0) {
        mType  = type;
        mIndex = id%hashbase;
        return true;
    }
    return false;
}

bool RedisDBIdx::SetErrInfo(const char *info, int len) {
    if (NULL == info) {
        return false;
    }
    if (NULL == mStrerr){
        mStrerr = new char[len + 1];
    }
    if (NULL != mStrerr) {
        strncpy(mStrerr, info, len);
        mStrerr[len] = '\0';
        return true;
    }
    return false;
}


xRedisClient::xRedisClient()
{
    mRedisPool = NULL;
}


xRedisClient::~xRedisClient()
{

}

bool xRedisClient::Init(unsigned int maxtype) {
    if(NULL==mRedisPool) {
        mRedisPool = new RedisPool;
        mRedisPool->Init(maxtype);
        return mRedisPool!=NULL;
    }
    return false;
}

void xRedisClient::release() {
    if (NULL!=mRedisPool) {
        mRedisPool->Release();
        delete mRedisPool;
        mRedisPool = NULL;
    }
}

void xRedisClient::KeepAlive() {
    if (NULL!=mRedisPool) {
        mRedisPool->KeepAlive();
    }
}

/*
RedisDBIdx xRedisClient::GetDBIndex(const char *key,  HASHFUN fun, const unsigned int type) {
    RedisDBIdx dbi;
    dbi.type  = type;
    unsigned int hashbase = mRedisPool->getHashBase(type);
    if (hashbase>0&& hashbase<MAX_REDIS_DB_HASHBASE) {
        dbi.isvalid = true;
        dbi.index   = fun(key)%hashbase;
        dbi.fun     = fun;
    }
    return dbi;
}

RedisDBIdx xRedisClient::GetDBIndex(const int64_t uid, const unsigned int type) {
    RedisDBIdx dbi;
    dbi.type  = type;
    unsigned int hashbase = mRedisPool->getHashBase(type);
    if (hashbase>0&& hashbase<MAX_REDIS_DB_HASHBASE) {
        dbi.isvalid = true;
        dbi.index   = uid%hashbase;
    }
    return dbi;
}
*/

inline RedisPool *xRedisClient::GetRedisPool() { 
    return mRedisPool;
}

bool xRedisClient::ConnectRedisCache( const RedisNode *redisnodelist, unsigned int hashbase, unsigned int cachetype) {
    if (NULL==mRedisPool) {
        return false;
    }
    
    if (!mRedisPool->setHashBase(cachetype, hashbase)) {
        return false;
    }
    
    for (unsigned int n = 0; n<hashbase; n++) {
        const RedisNode *pNode = &redisnodelist[n];
        if (NULL==pNode) {
            return false;
        }

        bool bRet = mRedisPool->ConnectRedisDB(cachetype, pNode->dbindex, pNode->host, pNode->port, 
            pNode->passwd, pNode->poolsize, pNode->timeout);
        if (!bRet) {
            return false;
        }
    }

    return true;
}


void xRedisClient::SetErrInfo(const RedisDBIdx& dbi, void *p) {
    if (NULL==p){
        SetErrString(dbi, CONNECT_CLOSED_ERROR, ::strlen(CONNECT_CLOSED_ERROR));
    } else {
        redisReply *reply = (redisReply*)p;
        SetErrString(dbi, reply->str, reply->len);
    }
}

void xRedisClient::SetErrString(const RedisDBIdx& dbi, const char *str, int len) {
    RedisDBIdx &dbindex = const_cast<RedisDBIdx&>(dbi);
    dbindex.SetErrInfo(str, len);
}

void xRedisClient::SetErrMessage(const RedisDBIdx& dbi, const char* fmt, ...)
{
    char szBuf[128] = { 0 };
    va_list va;
    va_start(va, fmt);
    vsnprintf(szBuf, sizeof(szBuf), fmt, va);
    va_end(va);
    SetErrString(dbi, szBuf, ::strlen(szBuf));
}

bool xRedisClient::command_bool(const RedisDBIdx& dbi, const char *cmd, ...) {
    bool bRet = false;
    RedisConn *pRedisConn = mRedisPool->GetConnection(dbi.mType, dbi.mIndex);
    if (NULL==pRedisConn) {
        SetErrString(dbi, GET_CONNECT_ERROR, ::strlen(GET_CONNECT_ERROR));
        return false;
    }

    va_list args;
    va_start(args, cmd);
    redisReply *reply = static_cast<redisReply *>(redisvCommand(pRedisConn->getCtx(), cmd, args));
    va_end(args);

    if (RedisPool::CheckReply(reply)) {
        if (REDIS_REPLY_STATUS==reply->type) {
            bRet = true;
        } else {
            bRet = (reply->integer == 1) ? true : false;
        }
    } else {
        SetErrInfo(dbi, reply);
    }

    RedisPool::FreeReply(reply);
    mRedisPool->FreeConnection(pRedisConn);

    return bRet;
}

bool xRedisClient::command_status(const RedisDBIdx& dbi, const char* cmd, ...) {
    bool bRet = false;
    RedisConn *pRedisConn = mRedisPool->GetConnection(dbi.mType, dbi.mIndex);
    if (NULL==pRedisConn) {
        SetErrString(dbi, GET_CONNECT_ERROR, ::strlen(GET_CONNECT_ERROR));
        return false;
    }

    va_list args;
    va_start(args, cmd);
    redisReply *reply = static_cast<redisReply *>(redisvCommand(pRedisConn->getCtx(), cmd, args));
    va_end(args);

    if (RedisPool::CheckReply(reply)) {
        bRet = true;
    } else {
        SetErrInfo(dbi, reply);
    }

    RedisPool::FreeReply(reply);
    mRedisPool->FreeConnection(pRedisConn);

    return bRet;
}

bool xRedisClient::command_integer(const RedisDBIdx& dbi, int64_t &retval, const char* cmd, ...) {
    bool bRet = false;
    RedisConn *pRedisConn = mRedisPool->GetConnection(dbi.mType, dbi.mIndex);
    if (NULL==pRedisConn) {
        SetErrString(dbi, GET_CONNECT_ERROR, ::strlen(GET_CONNECT_ERROR));
        return false;
    }

    va_list args;
    va_start(args, cmd);
    redisReply *reply = static_cast<redisReply *>(redisvCommand(pRedisConn->getCtx(), cmd, args));
    va_end(args);
    if (RedisPool::CheckReply(reply)) {
        retval = reply->integer;
        bRet = true;
    } else {
        SetErrInfo(dbi, reply);
    }

    RedisPool::FreeReply(reply);
    mRedisPool->FreeConnection(pRedisConn);

    return bRet;
}

bool xRedisClient::command_string(const RedisDBIdx& dbi, string &data, const char* cmd, ...) {
    bool bRet = false;
    RedisConn *pRedisConn = mRedisPool->GetConnection(dbi.mType, dbi.mIndex);
    if (NULL==pRedisConn) {
        SetErrString(dbi, GET_CONNECT_ERROR, ::strlen(GET_CONNECT_ERROR));
        return false;
    }

    va_list args;
    va_start(args, cmd);
    redisReply *reply = static_cast<redisReply *>(redisvCommand(pRedisConn->getCtx(), cmd, args));
    va_end(args);
    if (RedisPool::CheckReply(reply)) {
        data.assign(reply->str, reply->len);
        bRet = true;
    } else {
        SetErrInfo(dbi, reply);
    }

    RedisPool::FreeReply(reply);
    mRedisPool->FreeConnection(pRedisConn);

    return bRet;
}



bool xRedisClient::command_list(const RedisDBIdx& dbi, VALUES &vValue, const char* cmd, ...) {
    bool bRet = false;
    RedisConn *pRedisConn = mRedisPool->GetConnection(dbi.mType, dbi.mIndex);
    if (NULL==pRedisConn) {
        SetErrString(dbi, GET_CONNECT_ERROR, ::strlen(GET_CONNECT_ERROR));
        return false;
    }

    va_list args;
    va_start(args, cmd);
    redisReply *reply = static_cast<redisReply *>(redisvCommand(pRedisConn->getCtx(), cmd, args));
    va_end(args);
    if (RedisPool::CheckReply(reply)) {
        for (size_t i = 0; i<reply->elements; i++) {
            vValue.push_back(string(reply->element[i]->str, reply->element[i]->len));
        }
        bRet  = true;
    } else {
        SetErrInfo(dbi, reply);
    }

    RedisPool::FreeReply(reply);
    mRedisPool->FreeConnection(pRedisConn);

    return bRet;
}

bool xRedisClient::command_array(const RedisDBIdx& dbi,  ArrayReply& array,  const char* cmd, ...){
    bool bRet = false;
    RedisConn *pRedisConn = mRedisPool->GetConnection(dbi.mType, dbi.mIndex);
    if (NULL==pRedisConn) {
        SetErrString(dbi, GET_CONNECT_ERROR, ::strlen(GET_CONNECT_ERROR));
        return false;
    }

    va_list args;
    va_start(args, cmd);
    redisReply *reply = static_cast<redisReply *>(redisvCommand(pRedisConn->getCtx(), cmd, args));
    va_end(args);
    if (RedisPool::CheckReply(reply)) {
        for (size_t i = 0; i<reply->elements; i++) {
            DataItem item;
            item.type = reply->element[i]->type;
            item.str.assign(reply->element[i]->str, reply->element[i]->len);
            array.push_back(item);
        }
        bRet  = true;
    } else {
        SetErrInfo(dbi, reply);
    }

    RedisPool::FreeReply(reply);
    mRedisPool->FreeConnection(pRedisConn);
    return bRet;
}

bool xRedisClient::commandargv_bool(const RedisDBIdx& dbi, const VDATA& vData) {
    bool bRet = false;
    RedisConn *pRedisConn = mRedisPool->GetConnection(dbi.mType, dbi.mIndex);
    if (NULL==pRedisConn) {
        SetErrString(dbi, GET_CONNECT_ERROR, ::strlen(GET_CONNECT_ERROR));
        return bRet;
    }

    vector<const char *> argv( vData.size() );
    vector<size_t> argvlen( vData.size() );
    unsigned int j = 0;
    for ( VDATA::const_iterator i = vData.begin(); i != vData.end(); ++i, ++j ) {
        argv[j] = i->c_str(), argvlen[j] = i->size();
    }

    redisReply *reply = static_cast<redisReply *>(redisCommandArgv(pRedisConn->getCtx(), argv.size(), &(argv[0]), &(argvlen[0])));
    if (RedisPool::CheckReply(reply)) {
        bRet = (reply->integer==1)?true:false;
    } else {
        SetErrInfo(dbi, reply);
    }

    RedisPool::FreeReply(reply);
    mRedisPool->FreeConnection(pRedisConn);

    return bRet;
}

bool xRedisClient::commandargv_status(const RedisDBIdx& dbi, const VDATA& vData) {
    bool bRet = false;
    RedisConn *pRedisConn = mRedisPool->GetConnection(dbi.mType, dbi.mIndex);
    if (NULL==pRedisConn) {
        SetErrString(dbi, GET_CONNECT_ERROR, ::strlen(GET_CONNECT_ERROR));
        return bRet;
    }

    vector<const char *> argv( vData.size() );
    vector<size_t> argvlen( vData.size() );
    unsigned int j = 0;
    for ( VDATA::const_iterator i = vData.begin(); i != vData.end(); ++i, ++j ) {
        argv[j] = i->c_str(), argvlen[j] = i->size();
    }

    redisReply *reply = static_cast<redisReply *>(redisCommandArgv(pRedisConn->getCtx(), argv.size(), &(argv[0]), &(argvlen[0])));
    if (RedisPool::CheckReply(reply)) {
        bRet = true;
    } else {
        SetErrInfo(dbi, reply);
    }

    RedisPool::FreeReply(reply);
    mRedisPool->FreeConnection(pRedisConn);

    return bRet;
}

bool xRedisClient::commandargv_array(const RedisDBIdx& dbi, const VDATA& vDataIn, ArrayReply& array){
    bool bRet = false;
    RedisConn *pRedisConn = mRedisPool->GetConnection(dbi.mType, dbi.mIndex);
    if (NULL==pRedisConn) {
        SetErrString(dbi, GET_CONNECT_ERROR, ::strlen(GET_CONNECT_ERROR));
        return false;
    }

    vector<const char*> argv( vDataIn.size() );
    vector<size_t> argvlen( vDataIn.size() );
    unsigned int j = 0;
    for ( VDATA::const_iterator i = vDataIn.begin(); i != vDataIn.end(); ++i, ++j ) {
        argv[j] = i->c_str(), argvlen[j] = i->size();
    }

    redisReply *reply = static_cast<redisReply *>(redisCommandArgv(pRedisConn->getCtx(), argv.size(), &(argv[0]), &(argvlen[0])));
    if (RedisPool::CheckReply(reply)) {
        for (size_t i = 0; i<reply->elements; i++) {
            DataItem item;
            item.type = reply->element[i]->type;
            item.str.assign(reply->element[i]->str, reply->element[i]->len);
            //item.str = (NULL == reply->element[i]->str) ? string("") : reply->element[i]->str;
            array.push_back(item);
        }
        bRet  = true;
    } else {
        SetErrInfo(dbi, reply);
    }

    RedisPool::FreeReply(reply);
    mRedisPool->FreeConnection(pRedisConn);
    return bRet;
}

bool xRedisClient::commandargv_array(const RedisDBIdx& dbi, const VDATA& vDataIn, VALUES& array){
    bool bRet = false;
    RedisConn *pRedisConn = mRedisPool->GetConnection(dbi.mType, dbi.mIndex);
    if (NULL==pRedisConn) {
        SetErrString(dbi, GET_CONNECT_ERROR, ::strlen(GET_CONNECT_ERROR));
        return false;
    }

    vector<const char*> argv( vDataIn.size() );
    vector<size_t> argvlen( vDataIn.size() );
    unsigned int j = 0;
    for ( VDATA::const_iterator i = vDataIn.begin(); i != vDataIn.end(); ++i, ++j ) {
        argv[j] = i->c_str(), argvlen[j] = i->size();
    }

    redisReply *reply = static_cast<redisReply *>(redisCommandArgv(pRedisConn->getCtx(), argv.size(), &(argv[0]), &(argvlen[0])));
    if (RedisPool::CheckReply(reply)) {
        for (size_t i = 0; i<reply->elements; i++) {
            //string str = (NULL == reply->element[i]->str) ? string("") : reply->element[i]->str;
            string str(reply->element[i]->str, reply->element[i]->len);
            array.push_back(str);
        }
        bRet  = true;
    } else {
        SetErrInfo(dbi, reply);
    }

    RedisPool::FreeReply(reply);
    mRedisPool->FreeConnection(pRedisConn);
    return bRet;
}

bool xRedisClient::commandargv_integer(const RedisDBIdx& dbi, const VDATA& vDataIn, int64_t& retval){
    bool bRet = false;
    RedisConn *pRedisConn = mRedisPool->GetConnection(dbi.mType, dbi.mIndex);
    if (NULL==pRedisConn) {
        SetErrString(dbi, GET_CONNECT_ERROR, ::strlen(GET_CONNECT_ERROR));
        return false;
    }

    vector<const char*> argv( vDataIn.size() );
    vector<size_t> argvlen( vDataIn.size() );
    unsigned int j = 0;
    for ( VDATA::const_iterator iter = vDataIn.begin(); iter != vDataIn.end(); ++iter, ++j ) {
        argv[j] = iter->c_str(), argvlen[j] = iter->size();
    }

    redisReply *reply = static_cast<redisReply *>(redisCommandArgv(pRedisConn->getCtx(), argv.size(), &(argv[0]), &(argvlen[0])));
    if (RedisPool::CheckReply(reply)) {
        retval = reply->integer;
        bRet  = true;
    } else {
        SetErrInfo(dbi, reply);
    }

    RedisPool::FreeReply(reply);
    mRedisPool->FreeConnection(pRedisConn);
    return bRet;
}



