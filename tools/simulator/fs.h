/* SPDX-License-Identifier: Apache-2.0
 * 中文：把固件存储路径映射到模拟器专属目录，禁止路径穿越和符号链接。
 * English: Map firmware storage into the simulator, rejecting traversal and symlinks.
 */
#pragma once
#ifndef __ASSEMBLER__
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
int sim_open(const char *, int, ...);
FILE *sim_fopen(const char *, const char *);
int sim_stat(const char *, struct stat *, int);
DIR *sim_opendir(const char *);
int sim_mkdir(const char *, mode_t);
int sim_unlink(const char *);
int sim_rmdir(const char *);
int sim_rename(const char *, const char *);
int sim_access(const char *, int);
int sim_fsync(int);
#ifndef PICO_SIM_FS_IMPL
#define open(...) sim_open(__VA_ARGS__)
#define fopen(p,m) sim_fopen(p,m)
#define stat(p,s) sim_stat(p,s,0)
#define lstat(p,s) sim_stat(p,s,1)
#define opendir(p) sim_opendir(p)
#define mkdir(p,m) sim_mkdir(p,m)
#define unlink(p) sim_unlink(p)
#define remove(p) sim_unlink(p)
#define rmdir(p) sim_rmdir(p)
#define rename(a,b) sim_rename(a,b)
#define access(p,m) sim_access(p,m)
#define fsync(fd) sim_fsync(fd)
#endif

#endif
