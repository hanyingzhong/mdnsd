/*
 * Copyright (c) 2018  Joachim Nilsson <troglobit@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>

#include "mdnsd.h"

struct conf_srec {
	char   *type;

	char   *name;
	int     port;

	char   *txt[42];
	size_t  txt_num;
};


static char *chomp(char *str)
{
	char *p;

	if (!str || strlen(str) < 1) {
		errno = EINVAL;
		return NULL;
	}

	p = str + strlen(str) - 1;
        while (*p == '\n')
		*p-- = 0;

	return str;
}

static int match(char *key, char *token)
{
	return !strcmp(key, token);
}

static void read_line(char *line, struct conf_srec *srec)
{
	char *arg, *token;

	arg = chomp(line);
	DBG("Got line: '%s'", line);

	if (line[0] == '#')
		return;

	token = strsep(&arg, " \t");
	if (!token || !arg) {
		DBG("Skipping, token:%s, arg:%s", token, arg);
		return;
	}

	if (match(token, "type"))
		srec->type = strdup(arg);
	if (match(token, "name"))
		srec->name = strdup(arg);
	if (match(token, "port"))
		srec->port = atoi(arg);
//	if (match(token, "target"))
//		srec->target = strdup(arg);
	if (match(token, "txt") && srec->txt_num < NELEMS(srec->txt))
		srec->txt[srec->txt_num++] = strdup(arg);
}

static int parse(char *fn, struct conf_srec *srec)
{
	FILE *fp;
	char line[256];

	fp = fopen(fn, "r");
	if (!fp)
		return 1;

	while (fgets(line, sizeof(line), fp))
		read_line(line, srec);
	fclose(fp);
	DBG("Finished reading %s ...", fn);

	return 0;
}

int conf_init(mdns_daemon_t *d, char *fn)
{
	struct conf_srec srec;
	struct in_addr addr;
	unsigned char *packet;
	mdns_record_t *r;
	size_t i;
	xht_t *h;
	char hlocal[256], nlocal[256];
	char hostname[HOST_NAME_MAX];
	int len = 0;

	memset(&srec, 0, sizeof(srec));
	if (parse(fn, &srec)) {
		ERR("Failed reading %s: %s", fn, strerror(errno));
		return 1;
	}

	if (!srec.name) {
		gethostname(hostname, sizeof(hostname));
		srec.name = hostname;
	}

	if (!srec.type)
		srec.type = strdup("_http._tcp");

	snprintf(hlocal, sizeof(hlocal), "%s.%s.local.", srec.name, srec.type);
	snprintf(nlocal, sizeof(nlocal), "%s.local.", srec.name);

	// Announce that we have a $type service
	r = mdnsd_shared(d, "_services._dns-sd._udp.local.", QTYPE_PTR, 120);
	mdnsd_set_host(d, r, hlocal);

	r = mdnsd_shared(d, hlocal, QTYPE_PTR, 120);
	mdnsd_set_host(d, r, hlocal);

	r = mdnsd_unique(d, hlocal, QTYPE_SRV, 600, mdnsd_conflict, NULL);
	mdnsd_set_srv(d, r, 0, 0, srec.port, nlocal);

	r = mdnsd_unique(d, nlocal, QTYPE_A, 600, mdnsd_conflict, NULL);
	addr = mdnsd_get_address(d);
	mdnsd_set_raw(d, r, (char *)&addr, 4);
//	mdnsd_set_ip(d, r, mdnsd_get_address(d));

	r = mdnsd_unique(d, hlocal, QTYPE_TXT, 600, mdnsd_conflict, NULL);
	h = xht_new(11);
	for (i = 0; i < srec.txt_num; i++) {
		char *ptr;

		ptr = strchr(srec.txt[i], '=');
		if (!ptr)
			continue;
		*ptr++ = 0;

		xht_set(h, srec.txt[i], ptr);
	}
	packet = sd2txt(h, &len);
	xht_free(h);
	mdnsd_set_raw(d, r, (char *)packet, len);
	free(packet);

	return 0;
}
