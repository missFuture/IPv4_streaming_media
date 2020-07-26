#include <stdio.h>
#include <stdlib.h>
#include <glob.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <error.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "server_conf.h"
#include "medialib.h"
#include "../include/proto.h"
#include "mytbf.h"
#define DEBUG

#define PATHSIZE 1024
#define LINEBUFSIZE 1024
#define MP3_BITRATE 64*1024 //correct bps:128*1024
struct channel_context_st
{
    chnid_t chnid;
    char *desc;
    glob_t mp3glob;
    int pos;  //cur song
    int fd;  //file descriptor
    off_t offset;
    mytbf_t *tbf;
};

static struct channel_context_st channel[MAXCHIND + 1];

static struct channel_context_st *path2entry(const char* path)
{
    // path/desc.text  path/*.mp3
    syslog(LOG_INFO, "current path :%s\n", path);
    char pathstr[PATHSIZE] = {'\0'};
    char linebuf[LINEBUFSIZE];
    FILE *fp;
    struct channel_context_st *me;
    static chnid_t curr_id = MINCHNID;
    strcat(pathstr, path);
    strcat(pathstr, "/desc.txt");
    fp = fopen(pathstr, "r");
    syslog(LOG_INFO, "channel dir:%s\n", pathstr);
    if(fp == NULL)
    {
        syslog(LOG_INFO, "%s is not a channel dir(can't find desc.txt)", path);
        return NULL;
    }
    if(fgets(linebuf, LINEBUFSIZE, fp) == NULL)
    {
        syslog(LOG_INFO, "%s is not a channel dir(cant get the desc.text)", path);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    me = malloc(sizeof(*me));
    if(me == NULL)
    {
        syslog(LOG_ERR, "malloc():%s", strerror(errno));
        return NULL;
    }

    me->tbf = mytbf_init(MP3_BITRATE/8, MP3_BITRATE/8*5);
    if(me->tbf == NULL)
    {
        syslog(LOG_ERR, "mytbf_init():%s", strerror(errno));
        free(me);
        return NULL;
    }
    me->desc = strdup(linebuf);
    strncpy(pathstr, path, PATHSIZE);
    strncat(pathstr, "/*.mp3", PATHSIZE);
    if(glob(pathstr, 0, NULL, &me->mp3glob) != 0)
    {
        curr_id++;
        syslog(LOG_ERR, "%s is not a channel dir(can not find mp3 files", path);
        free(me);
        return NULL;
    }
    me->pos = 0;
    me->offset = 0;
    me->fd = open(me->mp3glob.gl_pathv[me->pos], O_RDONLY);
    if(me->fd <0 )
    {
        syslog(LOG_WARNING, "%s open failed.",me->mp3glob.gl_pathv[me->pos]);
        free(me);
        return NULL;
    }
    me->chnid = curr_id;
    curr_id++;
    return me;
}
int mlib_getchnlist(struct mlib_listentry_st **result, int *resnum)
{
    /*variable*/
    int num = 0;
    int i = 0;
    char path[PATHSIZE];
    glob_t globres;
    struct mlib_listentry_st *ptr;
    struct channel_context_st *res;

    for(int i = 0 ; i < MAXCHIND; i++)
    {
        channel[i].chnid = -1;
    }
    snprintf(path, PATHSIZE, "%s/*", server_conf.media_dir);
    #ifdef DEBUG
        printf("current path:%s\n", path);
    #endif
    if(glob(path, 0, NULL, &globres))
    {
        return -1;
    }
    #ifdef DEBUG
        printf("globres.gl_pathv[0]:%s\n", globres.gl_pathv[0]);
        printf("globres.gl_pathv[1]:%s\n", globres.gl_pathv[1]);
        printf("globres.gl_pathv[2]:%s\n", globres.gl_pathv[2]);
    #endif
    ptr = malloc(sizeof(struct mlib_listentry_st) * globres.gl_pathc);
    if(ptr == NULL)
    {
        syslog(LOG_ERR, "malloc() error.");
        exit(1);
    }
    for(i = 0; i < globres.gl_pathc; i++)
    {
        //globres.gl_pathv[i] -->"/var/media/ch1"
        res = path2entry(globres.gl_pathv[i]);//path-->record
        if(res != NULL)
        {
            syslog(LOG_ERR, "path2entry() return : %d %s.", res->chnid, res->desc);
            memcpy(channel+res->chnid, res, sizeof(*res));
            ptr[num].chnid = res->chnid;
            ptr[num].desc = strdup(res->desc);
            num++;
        }
    }
    *result = realloc(ptr, sizeof(struct mlib_listentry_st) * num);
    if(*result == NULL)
    {
        syslog(LOG_ERR, "realloc() failed.");
    }
    *resnum = num;
    return 0;
    

}

int mlib_freechnlist(struct mlib_listentry_st *ptr)
{
    free(ptr);
    return 0;
}
//当前是失败了或者已经读取完毕才会调用open_next
static int open_next(chnid_t chnid)
{
    //加入循环是为了防止每一首歌都打开失败，尽量都试着打开一次
    for(int i = 0 ; i < channel[chnid].mp3glob.gl_pathc;i++)
    {
        channel[chnid].pos++;
        //can open any file in channel[chnid].mp3glob.gl_pathv
        //所有的歌曲都没有打开
        if(channel[chnid].pos == channel[chnid].mp3glob.gl_pathc)
        {
            //channel[chnid].pos = 0;//再来一次
            return -1;
            break;//最后一首歌已经打开完毕，结束
        }
        close(channel[chnid].fd);
        channel[chnid].fd = open(channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], O_RDONLY);//对应频道的歌名
        //如果打开还是失败
        if(channel[chnid].fd < 0)
        {
            syslog(LOG_WARNING, "open(%s):%s", channel[chnid].mp3glob.gl_pathv[chnid], strerror(errno));
        }
        else//success
        {
            channel[chnid].offset = 0;
            return 0;
        }
    }
    syslog(LOG_ERR, "None of mp3 in channel %d id available.", chnid);

}
//从每个频道中读取内容，将当前播放的歌曲待发送的数据写到buf，实际大小为size
ssize_t mlib_readchn(chnid_t chnid, void *buf, size_t size)
{
    int tbfsize;
    int len;
    int next_ret = 0;
    //get token number
    tbfsize = mytbf_fetchtoken(channel[chnid].tbf, size);
    syslog(LOG_INFO, "current tbf():%d", mytbf_checktoken(channel[chnid].tbf));
    

    while(1)
    {
        len = pread(channel[chnid].fd, buf, tbfsize,channel[chnid].offset);
        /*current song open failed*/
        if(len < 0)
        {
            //当前这首歌可能有问题，错误不至于退出，读取下一首歌
            syslog(LOG_WARNING, "media file %s pread():%s", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], strerror(errno));
            open_next(chnid);
        }
        else if(len == 0)
        {
            syslog(LOG_DEBUG, "media %s file is over", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos]);
            #ifdef DEBUG
                printf("current chnid :%d\n", chnid);
            #endif
            next_ret = open_next(chnid);
            break;
        }
        else/*len > 0*///真正读取到了数据
        {
            channel[chnid].offset += len;
            syslog(LOG_DEBUG, "epoch : %f", (channel[chnid].offset) / (16*1000*1.024));
            break;
        }
    }
    //remain some token
    if(tbfsize - len > 0)
        mytbf_returntoken(channel[chnid].tbf, tbfsize - len);
    return len;//返回读取到的长度

    
}
