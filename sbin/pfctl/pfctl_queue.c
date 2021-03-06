/*	$OpenBSD: pfctl_queue.c,v 1.3 2017/04/26 15:50:59 mikeb Exp $ */

/*
 * Copyright (c) 2003 - 2013 Henning Brauer <henning@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>
#include <net/pfvar.h>
#include <arpa/inet.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/hfsc.h>

#include "pfctl.h"
#include "pfctl_parser.h"

#define AVGN_MAX	8
#define STAT_INTERVAL	5

struct queue_stats {
	struct hfsc_class_stats	 data;
	int			 avgn;
	double			 avg_bytes;
	double			 avg_packets;
	u_int64_t		 prev_bytes;
	u_int64_t		 prev_packets;
};

struct pfctl_queue_node {
	TAILQ_ENTRY(pfctl_queue_node)	entries;
	struct pf_queuespec		qs;
	struct queue_stats		qstats;
};
TAILQ_HEAD(qnodes, pfctl_queue_node) qnodes = TAILQ_HEAD_INITIALIZER(qnodes);

int			 pfctl_update_qstats(int);
void			 pfctl_insert_queue_node(const struct pf_queuespec,
			    const struct queue_stats);
struct pfctl_queue_node	*pfctl_find_queue_node(const char *, const char *);
void			 pfctl_print_queue_node(int, struct pfctl_queue_node *,
			    int);
void			 print_qstats(struct queue_stats);
void			 pfctl_free_queue_node(struct pfctl_queue_node *);
void			 pfctl_print_queue_nodestat(int,
			    const struct pfctl_queue_node *);
void			 update_avg(struct queue_stats *);
char			*rate2str(double);

int
pfctl_show_queues(int dev, const char *iface, int opts, int verbose2)
{
	struct pfctl_queue_node	*node;
	int			 nodes, dotitle = (opts & PF_OPT_SHOWALL);


	if ((nodes = pfctl_update_qstats(dev)) <= 0)
		return (nodes);

	TAILQ_FOREACH(node, &qnodes, entries) {
		if (iface != NULL && strcmp(node->qs.ifname, iface))
			continue;
		if (dotitle) {
			pfctl_print_title("QUEUES:");
			dotitle = 0;
		}
		pfctl_print_queue_node(dev, node, opts);
	}

	while (verbose2 && nodes > 0) {
		printf("\n");
		fflush(stdout);
		sleep(STAT_INTERVAL);
		if ((nodes = pfctl_update_qstats(dev)) == -1)
			return (-1);
		TAILQ_FOREACH(node, &qnodes, entries) {
			if (iface != NULL && strcmp(node->qs.ifname, iface))
				continue;
			pfctl_print_queue_node(dev, node, opts);
		}
	}
	while ((node = TAILQ_FIRST(&qnodes)) != NULL)
		TAILQ_REMOVE(&qnodes, node, entries);
	return (0);
}

int
pfctl_update_qstats(int dev)
{
	struct pfctl_queue_node	*node;
	struct pfioc_queue	 pq;
	struct pfioc_qstats	 pqs;
	u_int32_t		 mnr, nr;
	struct queue_stats	 qstats;
	static u_int32_t	 last_ticket;

	memset(&pq, 0, sizeof(pq));
	memset(&pqs, 0, sizeof(pqs));
	memset(&qstats, 0, sizeof(qstats));
	if (ioctl(dev, DIOCGETQUEUES, &pq)) {
		warn("DIOCGETQUEUES");
		return (-1);
	}

	/* if a new set is found, start over */
	if (pq.ticket != last_ticket)
		while ((node = TAILQ_FIRST(&qnodes)) != NULL)
			TAILQ_REMOVE(&qnodes, node, entries);
	last_ticket = pq.ticket;

	mnr = pq.nr;
	for (nr = 0; nr < mnr; ++nr) {
		pqs.nr = nr;
		pqs.ticket = pq.ticket;
		pqs.buf = &qstats.data;
		pqs.nbytes = sizeof(qstats.data);
		if (ioctl(dev, DIOCGETQSTATS, &pqs)) {
			warn("DIOCGETQSTATS");
			return (-1);
		}
		if ((node = pfctl_find_queue_node(pqs.queue.qname,
		    pqs.queue.ifname)) != NULL) {
			memcpy(&node->qstats.data, &qstats.data,
			    sizeof(qstats.data));
			update_avg(&node->qstats);
		} else {
			pfctl_insert_queue_node(pqs.queue, qstats);
		}
	}
	return (mnr);
}

void
pfctl_insert_queue_node(const struct pf_queuespec qs,
    const struct queue_stats qstats)
{
	struct pfctl_queue_node	*node;

	node = calloc(1, sizeof(struct pfctl_queue_node));
	if (node == NULL)
		err(1, "pfctl_insert_queue_node: calloc");
	memcpy(&node->qs, &qs, sizeof(qs));
	memcpy(&node->qstats, &qstats, sizeof(qstats));
	TAILQ_INSERT_TAIL(&qnodes, node, entries);
	update_avg(&node->qstats);
}

struct pfctl_queue_node *
pfctl_find_queue_node(const char *qname, const char *ifname)
{
	struct pfctl_queue_node	*node;

	TAILQ_FOREACH(node, &qnodes, entries)
		if (!strcmp(node->qs.qname, qname)
		    && !(strcmp(node->qs.ifname, ifname)))
			return (node);
	return (NULL);
}

void
pfctl_print_queue_node(int dev, struct pfctl_queue_node *node, int opts)
{
	if (node == NULL)
		return;

	print_queuespec(&node->qs);
	if (opts & PF_OPT_VERBOSE)
		pfctl_print_queue_nodestat(dev, node);

	if (opts & PF_OPT_DEBUG)
		printf("  [ qid=%u parent_qid=%u ifname=%s]\n",
		    node->qs.qid, node->qs.parent_qid, node->qs.ifname);
}

void
pfctl_print_queue_nodestat(int dev, const struct pfctl_queue_node *node)
{
	printf("  [ pkts: %10llu  bytes: %10llu  "
	    "dropped pkts: %6llu bytes: %6llu ]\n",
	    (unsigned long long)node->qstats.data.xmit_cnt.packets,
	    (unsigned long long)node->qstats.data.xmit_cnt.bytes,
	    (unsigned long long)node->qstats.data.drop_cnt.packets,
	    (unsigned long long)node->qstats.data.drop_cnt.bytes);
	printf("  [ qlength: %3d/%3d ]\n", node->qstats.data.qlength,
	    node->qstats.data.qlimit);

	if (node->qstats.avgn < 2)
		return;

	printf("  [ measured: %7.1f packets/s, %s/s ]\n",
	    node->qstats.avg_packets / STAT_INTERVAL,
	    rate2str((8 * node->qstats.avg_bytes) / STAT_INTERVAL));
}

void
update_avg(struct queue_stats *s)
{
	if (s->avgn > 0) {
		if (s->data.xmit_cnt.bytes >= s->prev_bytes)
			s->avg_bytes = ((s->avg_bytes * (s->avgn - 1)) +
			    (s->data.xmit_cnt.bytes - s->prev_bytes)) /
			    s->avgn;
		if (s->data.xmit_cnt.packets >= s->prev_packets)
			s->avg_packets = ((s->avg_packets * (s->avgn - 1)) +
			    (s->data.xmit_cnt.packets - s->prev_packets)) /
			    s->avgn;
	}

	s->prev_bytes = s->data.xmit_cnt.bytes;
	s->prev_packets = s->data.xmit_cnt.packets;
	if (s->avgn < AVGN_MAX)
		s->avgn++;
}

#define	R2S_BUFS	8
#define	RATESTR_MAX	16

char *
rate2str(double rate)
{
	char		*buf;
	static char	 r2sbuf[R2S_BUFS][RATESTR_MAX];  /* ring bufer */
	static int	 idx = 0;
	int		 i;
	static const char unit[] = " KMG";

	buf = r2sbuf[idx++];
	if (idx == R2S_BUFS)
		idx = 0;

	for (i = 0; rate >= 1000 && i <= 3; i++)
		rate /= 1000;

	if ((int)(rate * 100) % 100)
		snprintf(buf, RATESTR_MAX, "%.2f%cb", rate, unit[i]);
	else
		snprintf(buf, RATESTR_MAX, "%d%cb", (int)rate, unit[i]);

	return (buf);
}
