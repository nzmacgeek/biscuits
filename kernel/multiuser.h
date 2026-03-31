#pragma once
// BlueyOS Multi-User System
// "Who's playing today?" - Bluey
// Unix-style user database (passwd + shadow files)
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"

#define MAX_USERS       16
#define MAX_USERNAME    32
#define MAX_GECOS       64
#define MAX_HOME        64
#define MAX_SHELL       32
#define MAX_GROUP_NAME  32
#define MAX_GROUPS      16
#define MAX_GROUP_MEMBERS 8

// Mirrors /etc/passwd format:
//   username:x:uid:gid:gecos:home:shell
typedef struct {
    char     username[MAX_USERNAME];
    uint32_t uid;
    uint32_t gid;
    char     gecos[MAX_GECOS];   // full name / GECOS comment
    char     home[MAX_HOME];
    char     shell[MAX_SHELL];
} passwd_entry_t;

// Mirrors /etc/shadow format:
//   username:$sha256$salt$hash:last_changed:min:max:warn:::
// hash field is "*" (no password) or "!" (locked) or $sha256$salt$hash
typedef struct {
    char     username[MAX_USERNAME];
    char     hash[128];          // full hash string
    uint32_t last_changed;       // days since Bandit epoch
    uint32_t min_age;
    uint32_t max_age;
    uint32_t warn_days;
} shadow_entry_t;

// Mirrors /etc/group format:
//   groupname:x:gid:user1,user2,...
typedef struct {
    char     name[MAX_GROUP_NAME];
    uint32_t gid;
    char     members[MAX_GROUP_MEMBERS][MAX_USERNAME];
} group_entry_t;

void      multiuser_init(void);
int       multiuser_login(const char *username, const char *password,
                          uint32_t *uid_out, uint32_t *gid_out);
int       multiuser_authenticate(const char *username, const char *password);
int       multiuser_get_passwd(uint32_t uid, passwd_entry_t *out);
int       multiuser_get_passwd_by_name(const char *name, passwd_entry_t *out);
int       multiuser_is_user_in_group(uint32_t uid, uint32_t gid);
size_t    multiuser_get_groups(uint32_t uid, uint32_t gid, uint32_t *out, size_t max);
uint32_t  multiuser_current_uid(void);
uint32_t  multiuser_current_gid(void);
void      multiuser_set_current(uint32_t uid, uint32_t gid);
int       multiuser_is_root(void);
void      multiuser_print_passwd(void);   // dump /etc/passwd-style output
void      multiuser_print_shadow(void);   // dump /etc/shadow-style output (hashes only)
