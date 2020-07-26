#include <syslog.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "server_conf.h"
#include "medialib.h"
#include "../include/proto.h"

static int tid_nextpos = 0;
//each record,每一个线程负责一个频道
struct thr_channel_ent_st
{
    chnid_t chnid;
    pthread_t tid;

};
//thr_channel是一个结构体数组，每一个元素表示一个结构体，结构题内容：频道号，处理该频道线程号
struct thr_channel_ent_st thr_channel[CHANNR];

//对应频道的线程的处理函数
static void *thr_channel_snder(void* ptr)
{
    struct msg_channel_st *sbufp;
    int len;
    struct mlib_listentry_st *ent = ptr;//chnid, desc
    sbufp = malloc(MSG_CHANNEL_MAX);
    if(sbufp == NULL)
    {
        syslog(LOG_ERR, "malloc():%s", strerror(errno));
        exit(1);
    }
    sbufp->chnid = ent->chnid;//频道号处理
    //频道内容读取
    while(1)
    {
        //len = mlib_readchn(ent->chnid, sbufp->data, MAX_DATA);
        len = mlib_readchn(ent->chnid, sbufp->data, 128*1024/8);
        //if(ent->chnid == 3)
        syslog(LOG_DEBUG, "mlib_readchn() len: %d", len);
        //len+sizeof(chnid_t)表示需要发送的数据的长度
        if(sendto(serversd, sbufp, len + sizeof(chnid_t), 0, (void*)&sndaddr, sizeof(sndaddr)) < 0)
        {
            syslog(LOG_ERR, "thr_channel(%d):sendto():%s", ent->chnid, strerror(errno));
            break;

        }
        sched_yield();
    }
    pthread_exit(NULL);

}
//创建一个处理频道的线程
int thr_channel_create(struct mlib_listentry_st *ptr)
{
    int err;
    err = pthread_create(&thr_channel[tid_nextpos].tid, NULL, thr_channel_snder, ptr);
    if(err)
    {
        syslog(LOG_WARNING, "pthread_create():%s", strerror(err));
        return -err;
    }
    thr_channel[tid_nextpos].chnid = ptr->chnid;//填写频道信息
    tid_nextpos++;
    return 0;

}
//销毁对应频道线程
int thr_channel_destroy(struct mlib_listentry_st *ptr)
{
    for(int i = 0; i < CHANNR; i++)
    {
        if(thr_channel[i].chnid == ptr->chnid)
            {
                if(pthread_cancel(thr_channel[i].tid) < 0)
                {
                    syslog(LOG_ERR, "pthread_cancel():thr thread of channel%d", ptr->chnid);
                    return -ESRCH;
                }
            }
        pthread_join(thr_channel[i].tid, NULL);
        thr_channel[i].chnid = -1;
        return 0;
    }
}
//销毁全部频道线程
int thr_channel_destroyall(void)
{
    for(int i = 0; i < CHANNR; i++)
    {
        if(thr_channel[i].chnid > 0)
        {
            if(pthread_cancel(thr_channel[i].tid) < 0)
            {
                syslog(LOG_ERR, "pthread_cancel():thr thread of channel:%ld", thr_channel[i].tid);
                return -ESRCH;
            }
            pthread_join(thr_channel[i].tid, NULL);
            thr_channel[i].chnid =  -1;
        }
    }
}
