/* $OpenBSD: i8253.h,v 1.5 2017/04/28 08:14:48 mlarkin Exp $ */
/*
 * Copyright (c) 2016 Mike Larkin <mlarkin@openbsd.org>
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

/*
 * Emulated i8253 PIT (counter)
 */
#define TIMER_BASE	0x40	/* 8253 timer 0 */
#define TIMER_CTRL	0x43	/* 8253 timer control */
#define NS_PER_TICK	(1000000000 / TIMER_FREQ)
#define TIMER_RB_COUNT	0x20	/* read back count value */
#define TIMER_RB_STATUS	0x10	/* read back status */
#define TIMER_RB_C0	0x2	/* read back channel 0 */
#define TIMER_RB_C1	0x4	/* read back channel 1 */
#define TIMER_RB_C2	0x8	/* read back channel 1 */

/* i8253 registers */
struct i8253_channel {
	struct timeval tv;	/* timer start time */
	uint16_t start;		/* starting value */
	uint16_t olatch;	/* output latch */
	uint16_t ilatch;	/* input latch */
	uint8_t last_r;		/* last read byte (MSB/LSB) */
	uint8_t last_w;		/* last written byte (MSB/LSB) */
	uint8_t mode;		/* counter mode */
	uint8_t rbs;		/* channel is in readback status mode */
	struct event timer;	/* timer event for this counter */
	uint32_t vm_id;		/* owning VM id */
};

void i8253_init(uint32_t);
void i8253_reset(uint8_t);
void i8253_fire(int, short, void *);
uint8_t vcpu_exit_i8253(struct vm_run_params *);
void i8253_do_readback(uint32_t);
