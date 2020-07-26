#ifndef SERVER_CONF_H_
#define SERVER_CONF_H_

#define DEFAULT_MEDIADIR "/var/media"
#define DEFAULT_IF "enp0s3"

enum
{
    RUN_DAEMON = 1,
    RUN_FOREGROUND
};
struct server_conf_st
{
    char *rcvport;
    char *mgroup;
    char *media_dir;
    char runmode;
    char *ifname;
};
extern struct server_conf_st server_conf;
extern int serversd;
extern struct sockaddr_in sndaddr;
#endif