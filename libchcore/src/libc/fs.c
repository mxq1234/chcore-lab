/*
 * Copyright (c) 2022 Institute of Parallel And Distributed Systems (IPADS)
 * ChCore-Lab is licensed under the Mulan PSL v1.
 * You can use this software according to the terms and conditions of the Mulan PSL v1.
 * You may obtain a copy of Mulan PSL v1 at:
 *     http://license.coscl.org.cn/MulanPSL
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v1 for more details.
 */

#include <stdio.h>
#include <string.h>
#include <chcore/types.h>
#include <chcore/fsm.h>
#include <chcore/tmpfs.h>
#include <chcore/ipc.h>
#include <chcore/internal/raw_syscall.h>
#include <chcore/internal/server_caps.h>
#include <chcore/procm.h>
#include <chcore/fs/defs.h>

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, l)   __builtin_va_arg(v, l)
#define va_copy(d, s)  __builtin_va_copy(d, s)


extern struct ipc_struct *fs_ipc_struct;

/* You could add new functions or include headers here.*/
/* LAB 5 TODO BEGIN */

#include <libc/stdio.h>

bool isBlank(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool isNumber(char c) {
	return c >= '0' && c <= '9';
}

int atoi(char* str) {
	int ret = 0;
	while(*str) {
		ret = ret * 10 + (*str - '0');
		str++;
	}
	return ret;
}

void itoa(int num, char* str) {
	int i = 0, j = 0;
	char tmp[32];
	while(num) {
		tmp[i++] = num % 10 + '0';
		num /= 10;
	}
	for(i = i - 1, j = 0; i >= 0; i--, j++)
		str[j] = tmp[i];
	str[j] = '\0';
}

int open(const char *pathname, int flags) {
	/* LAB 5 TODO BEGIN */
	struct ipc_struct* icb = fs_ipc_struct;
	struct ipc_msg* msg = ipc_create_msg(icb, sizeof(struct fs_request), 0);
	struct fs_request* fr = (struct fs_request*)ipc_get_msg_data(msg);
	s64 ret;

	fr->req = FS_REQ_OPEN;
	
	size_t len = strlen(pathname);
	if(len > FS_REQ_PATH_BUF_LEN) {
		ipc_destroy_msg(icb, msg);
		return -1;
	}
	memcpy(fr->open.pathname, pathname, len);
	fr->open.pathname[len] = '\0';

	fr->open.new_fd = alloc_fd();
	fr->open.mode = 0;
	fr->open.flags = flags;

	ret = ipc_call(icb, msg);
	ipc_destroy_msg(icb, msg);
	return ret;
}

int close(int fd) {
	struct ipc_struct* icb = fs_ipc_struct;
	struct ipc_msg* msg = ipc_create_msg(icb, sizeof(struct fs_request), 0);
	struct fs_request* fr = (struct fs_request*)ipc_get_msg_data(msg);
	s64 ret;

	fr->req = FS_REQ_CLOSE;

	fr->close.fd = fd;
	ret = ipc_call(icb, msg);
	ipc_destroy_msg(icb, msg);
	if(ret < 0)	return -1;
	return 0;
}

/* LAB 5 TODO END */


FILE *fopen(const char * filename, const char * mode) {

	/* LAB 5 TODO BEGIN */
	struct FILE* f = calloc(1, sizeof(struct FILE));
	int ret, flags;

	if(strcmp(mode, "r") == 0) {
		flags = O_RDONLY;
	} else if(strcmp(mode, "w") == 0) {
		flags = O_WRONLY;
	} else {
		goto error;
	}

	ret = open(filename, flags);
	if(ret < 0)
		goto error;
	f->fd = ret;
	f->flags = flags;
	/* LAB 5 TODO END */
    return f;

error:
	free(f);
	return NULL;
}

size_t fwrite(const void * src, size_t size, size_t nmemb, FILE * f) {

	/* LAB 5 TODO BEGIN */
	if(f->flags != O_WRONLY)
		return 0;

	struct ipc_struct* icb = fs_ipc_struct;
	struct ipc_msg* msg = ipc_create_msg(icb, sizeof(struct fs_request) + size * nmemb, 0);
	struct fs_request* fr = (struct fs_request*)ipc_get_msg_data(msg);
	size_t ret;

	fr->req = FS_REQ_WRITE;

	fr->write.fd = f->fd;
	fr->write.count = size * nmemb;
	memcpy((void *)fr + sizeof(struct fs_request), src, size * nmemb);
	
	ret = ipc_call(icb, msg);
	ipc_destroy_msg(icb, msg);
	/* LAB 5 TODO END */
    return ret / size;
}

size_t fread(void * destv, size_t size, size_t nmemb, FILE * f) {

	/* LAB 5 TODO BEGIN */
	if(f->flags != O_RDONLY)
		return 0;

	struct ipc_struct* icb = fs_ipc_struct;
	struct ipc_msg* msg = ipc_create_msg(icb, sizeof(struct fs_request) + size * nmemb, 0);
	struct fs_request* fr = (struct fs_request*)ipc_get_msg_data(msg);
	size_t ret;

	fr->req = FS_REQ_READ;

	fr->read.fd = f->fd;
	fr->read.count = size * nmemb;
	ret = ipc_call(icb, msg);
	memcpy(destv, (void*)fr, ret);
	ipc_destroy_msg(icb, msg);
	/* LAB 5 TODO END */
    return ret / size;
}

int fclose(FILE *f) {

	/* LAB 5 TODO BEGIN */
	int ret = close(f->fd);
	free(f);
	/* LAB 5 TODO END */
    return ret;
}

/* Need to support %s and %d. */
int fscanf(FILE * f, const char * fmt, ...) {

	/* LAB 5 TODO BEGIN */
	char buf[1024];
	int len = 0, ret = 0;
	va_list args;
	va_start(args, fmt);
	len = fread(buf, 1, 1024, f);

	// vsscanf(buf, fmt, args);
	size_t bi = 0, fi = 0;
	while(bi < len && fmt[fi] != '\0') {
		if(fmt[fi] == ' ') {
			while(bi < len && buf[bi] == ' ') bi++;
			while(fmt[fi] == ' ') fi++;
			continue;
		}
		if(fmt[fi] == '%') {
			if(fmt[fi + 1] == 's') {
				char* str = va_arg(args, char*);
				size_t j;
				for(j = 0; bi + j < len && !isBlank(buf[bi + j]); j++)
					str[j] = buf[bi + j];
				str[j] = '\0';
				fi += 2;
				bi += j;
				ret++;
			} else if(fmt[fi + 1] == 'd') {
				int* num = va_arg(args, int*);
				size_t j;
				char tmp[12];
				for(j = 0; bi + j < len && isNumber(buf[bi + j]); j++)
					tmp[j] = buf[bi + j];
				tmp[j] = '\0';
				*num = atoi(tmp);
				fi += 2;
				bi += j;
				ret++;
			} else {
				break;
			}
		} else {
			if(buf[bi] != fmt[fi])	break;
			bi++;
			fi++;
		}
	}
	
	va_end(args);
	/* LAB 5 TODO END */
    return ret;
}

/* Need to support %s and %d. */
int fprintf(FILE * f, const char * fmt, ...) {

	/* LAB 5 TODO BEGIN */
	char buf[1024];
	int ret;
	va_list args;
	va_start(args, fmt);
	
	// vsprintf(buf, fmt, &args);
	size_t bi = 0, fi = 0;
	while(fmt[fi] != '\0') {
		if(fmt[fi] == '%') {
			if(fmt[fi + 1] == 's') {
				char* str = va_arg(args, char*);
				size_t j;
				for(j = 0; str[j] != '\0'; j++)
					buf[bi + j] = str[j];
				fi += 2;
				bi += j;
			} else if(fmt[fi + 1] == 'd') {
				int num = va_arg(args, int);
				char tmp[12];
				size_t j;
				itoa(num, tmp);
				for(j = 0; tmp[j] != '\0'; j++)
					buf[bi + j] = tmp[j];
				fi += 2;
				bi += j;
			} else {
				break;
			}
		} else {
			buf[bi++] = fmt[fi++];
		}
	}

	ret = fwrite(buf, bi, 1, f);
	va_end(args);
	/* LAB 5 TODO END */
    return ret;
}

