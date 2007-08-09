/**
 * @file
 * Dynamic pool memory manager
 *
 */

/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

#include <string.h>

#include "lwip/opt.h"
#include "lwip/memp.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/raw.h"
#include "lwip/tcp.h"
#include "lwip/api.h"
#include "lwip/api_msg.h"
#include "lwip/tcpip.h"
#include "lwip/sys.h"
#include "lwip/stats.h"

#if ARP_QUEUEING
#include "netif/etharp.h"
#endif

struct memp {
  struct memp *next;
#if MEMP_OVERFLOW_CHECK
  const char *file;
  int line;
#endif /* MEMP_OVERFLOW_CHECK */
};

static struct memp *memp_tab[MEMP_MAX];

#if MEMP_OVERFLOW_CHECK
/* if MEMP_OVERFLOW_CHECK is turned on, we reserve some bytes at the beginning
 * and at the end of each element, initialize them as 0xcd and check
 * them later. */
/* If MEMP_OVERFLOW_CHECK is >= 2, on every call to memp_malloc or memp_free,
 * every single element in each pool is checked!
 * This is VERY SLOW but also very helpful. */
/* MEMP_SANITY_REGION_BEFORE and MEMP_SANITY_REGION_AFTER can be overridden in
 * lwipopts.h to change the amount reserved for checking. */
#ifndef MEMP_SANITY_REGION_BEFORE
#define MEMP_SANITY_REGION_BEFORE  16
#endif /* MEMP_SANITY_REGION_BEFORE*/
#if MEMP_SANITY_REGION_BEFORE > 0
#define MEMP_SANITY_REGION_BEFORE_ALIGNED    LWIP_MEM_ALIGN_SIZE(MEMP_SANITY_REGION_BEFORE)
#else
#define MEMP_SANITY_REGION_BEFORE_ALIGNED    0
#endif /* MEMP_SANITY_REGION_BEFORE*/
#ifndef MEMP_SANITY_REGION_AFTER
#define MEMP_SANITY_REGION_AFTER   16
#endif /* MEMP_SANITY_REGION_AFTER*/
#if MEMP_SANITY_REGION_AFTER > 0
#define MEMP_SANITY_REGION_AFTER_ALIGNED     LWIP_MEM_ALIGN_SIZE(MEMP_SANITY_REGION_AFTER)
#else
#define MEMP_SANITY_REGION_AFTER_ALIGNED     0
#endif /* MEMP_SANITY_REGION_AFTER*/

/* MEMP_SIZE: save space for struct memp and for sanity check */
#define MEMP_SIZE          (LWIP_MEM_ALIGN_SIZE(sizeof(struct memp)) + MEMP_SANITY_REGION_BEFORE_ALIGNED)
#define MEMP_ALIGN_SIZE(x) (LWIP_MEM_ALIGN_SIZE(x) + MEMP_SANITY_REGION_AFTER_ALIGNED)

#else /* MEMP_OVERFLOW_CHECK */

/* No sanity checks
 * We don't need to preserve the struct memp while not allocated, so we
 * can save a little space and set MEMP_SIZE to 0.
 */
#define MEMP_SIZE           0
#define MEMP_ALIGN_SIZE(x) (LWIP_MEM_ALIGN_SIZE(x))

#endif /* MEMP_OVERFLOW_CHECK */

#if !MEM_USE_POOLS
static
#endif
const u16_t memp_sizes[MEMP_MAX] = {
  MEMP_ALIGN_SIZE(sizeof(struct pbuf)),
  MEMP_ALIGN_SIZE(sizeof(struct raw_pcb)),
  MEMP_ALIGN_SIZE(sizeof(struct udp_pcb)),
  MEMP_ALIGN_SIZE(sizeof(struct tcp_pcb)),
  MEMP_ALIGN_SIZE(sizeof(struct tcp_pcb_listen)),
  MEMP_ALIGN_SIZE(sizeof(struct tcp_seg)),
  MEMP_ALIGN_SIZE(sizeof(struct netbuf)),
  MEMP_ALIGN_SIZE(sizeof(struct netconn)),
  MEMP_ALIGN_SIZE(sizeof(struct tcpip_msg)),
#if ARP_QUEUEING
  MEMP_ALIGN_SIZE(sizeof(struct etharp_q_entry)),
#endif
  MEMP_ALIGN_SIZE(sizeof(struct pbuf)) + MEMP_ALIGN_SIZE(PBUF_POOL_BUFSIZE),
  MEMP_ALIGN_SIZE(sizeof(struct sys_timeo)),
#if MEM_USE_POOLS
  MEMP_ALIGN_SIZE(MEM_POOL_SIZE_1),
  MEMP_ALIGN_SIZE(MEM_POOL_SIZE_2),
  MEMP_ALIGN_SIZE(MEM_POOL_SIZE_3),
  MEMP_ALIGN_SIZE(MEM_POOL_SIZE_4),
#endif
};

static const u16_t memp_num[MEMP_MAX] = {
  MEMP_NUM_PBUF,
  MEMP_NUM_RAW_PCB,
  MEMP_NUM_UDP_PCB,
  MEMP_NUM_TCP_PCB,
  MEMP_NUM_TCP_PCB_LISTEN,
  MEMP_NUM_TCP_SEG,
  MEMP_NUM_NETBUF,
  MEMP_NUM_NETCONN,
  MEMP_NUM_TCPIP_MSG,
#if ARP_QUEUEING
  MEMP_NUM_ARP_QUEUE,
#endif
  PBUF_POOL_SIZE,
  MEMP_NUM_SYS_TIMEOUT,
#if MEM_USE_POOLS
  MEM_POOL_NUM_1,
  MEM_POOL_NUM_2,
  MEM_POOL_NUM_3,
  MEM_POOL_NUM_4,
#endif
};

#define MEMP_TYPE_SIZE(qty, type) \
  ((qty) * (MEMP_SIZE + MEMP_ALIGN_SIZE(sizeof(type))))

static u8_t memp_memory[MEM_ALIGNMENT - 1 + 
  MEMP_TYPE_SIZE(MEMP_NUM_PBUF, struct pbuf) +
  MEMP_TYPE_SIZE(MEMP_NUM_RAW_PCB, struct raw_pcb) +
  MEMP_TYPE_SIZE(MEMP_NUM_UDP_PCB, struct udp_pcb) +
  MEMP_TYPE_SIZE(MEMP_NUM_TCP_PCB, struct tcp_pcb) +
  MEMP_TYPE_SIZE(MEMP_NUM_TCP_PCB_LISTEN, struct tcp_pcb_listen) +
  MEMP_TYPE_SIZE(MEMP_NUM_TCP_SEG, struct tcp_seg) +
  MEMP_TYPE_SIZE(MEMP_NUM_NETBUF, struct netbuf) +
  MEMP_TYPE_SIZE(MEMP_NUM_NETCONN, struct netconn) +
  MEMP_TYPE_SIZE(MEMP_NUM_TCPIP_MSG, struct tcpip_msg) +
#if ARP_QUEUEING
  MEMP_TYPE_SIZE(MEMP_NUM_ARP_QUEUE, struct etharp_q_entry) +
#endif
  MEMP_TYPE_SIZE(PBUF_POOL_SIZE, struct pbuf) +
                 ((PBUF_POOL_SIZE) * MEMP_ALIGN_SIZE(PBUF_POOL_BUFSIZE)) +
  MEMP_TYPE_SIZE(MEMP_NUM_SYS_TIMEOUT, struct sys_timeo)
#if MEM_USE_POOLS
  + ((MEM_POOL_NUM_1) * MEMP_ALIGN_SIZE(MEM_POOL_SIZE_1))
  + ((MEM_POOL_NUM_2) * MEMP_ALIGN_SIZE(MEM_POOL_SIZE_2))
  + ((MEM_POOL_NUM_3) * MEMP_ALIGN_SIZE(MEM_POOL_SIZE_3))
  + ((MEM_POOL_NUM_4) * MEMP_ALIGN_SIZE(MEM_POOL_SIZE_4))
#endif
];

#if MEMP_SANITY_CHECK
/**
 * Check that memp-lists don't form a circle
 */
static int
memp_sanity(void)
{
  s16_t i, c;
  struct memp *m, *n;

  for (i = 0; i < MEMP_MAX; i++) {
    for (m = memp_tab[i]; m != NULL; m = m->next) {
      c = 1;
      for (n = memp_tab[i]; n != NULL; n = n->next) {
        if (n == m && --c < 0) {
          return 0;
        }
      }
    }
  }
  return 1;
}
#endif /* MEMP_SANITY_CHECK*/
#if MEMP_OVERFLOW_CHECK
/**
 * Check if a memp element was victim of an overflow
 * (e.g. the restricted area after it has been altered)
 *
 * @param p the memp element to check
 * @param memp_size the element size of the pool p comes from
 */
static void
memp_overflow_check_element(struct memp *p, u16_t memp_size)
{
  u16_t k;
  u8_t *m;
#if MEMP_SANITY_REGION_BEFORE_ALIGNED > 0
  m = (u8_t*)p + MEMP_SIZE - MEMP_SANITY_REGION_BEFORE_ALIGNED;
  for (k = 0; k < MEMP_SANITY_REGION_BEFORE_ALIGNED; k++) {
    if (m[k] != 0xcd) {
      LWIP_ASSERT("detected memp underflow!", 0);
    }
  }
#endif
#if MEMP_SANITY_REGION_AFTER_ALIGNED > 0
  m = (u8_t*)p + MEMP_SIZE + memp_size - MEMP_SANITY_REGION_AFTER_ALIGNED;
  for (k = 0; k < MEMP_SANITY_REGION_AFTER_ALIGNED; k++) {
    if (m[k] != 0xcd) {
      LWIP_ASSERT("detected memp overflow!", 0);
    }
  }
#endif
}
/**
 * Do an overflow check for all elements in every pool.
 *
 * @see memp_overflow_check_element for a description of the check
 */
static void
memp_overflow_check_all(void)
{
  u16_t i, j;
  struct memp *p;

  p = LWIP_MEM_ALIGN(memp_memory);
  for (i = 0; i < MEMP_MAX; ++i) {
    p = p;
    for (j = 0; j < memp_num[i]; ++j) {
      memp_overflow_check_element(p, memp_sizes[i]);
      p = (struct memp*)((u8_t*)p + MEMP_SIZE + memp_sizes[i]);
    }
  }
}
/**
 * Initialize the restricted areas of all memp elements in every pool.
 */
static void
memp_overflow_init(void)
{
  u16_t i, j;
  struct memp *p;
  u8_t *m;

  p = LWIP_MEM_ALIGN(memp_memory);
  for (i = 0; i < MEMP_MAX; ++i) {
    p = p;
    for (j = 0; j < memp_num[i]; ++j) {
#if MEMP_SANITY_REGION_BEFORE_ALIGNED > 0
      m = (u8_t*)p + MEMP_SIZE - MEMP_SANITY_REGION_BEFORE_ALIGNED;
      memset(m, 0xcd, MEMP_SANITY_REGION_BEFORE_ALIGNED);
#endif
#if MEMP_SANITY_REGION_AFTER_ALIGNED > 0
      m = (u8_t*)p + MEMP_SIZE + memp_sizes[i] - MEMP_SANITY_REGION_AFTER_ALIGNED;
      memset(m, 0xcd, MEMP_SANITY_REGION_AFTER_ALIGNED);
#endif
      p = (struct memp*)((u8_t*)p + MEMP_SIZE + memp_sizes[i]);
    }
  }
}
#endif /* MEMP_OVERFLOW_CHECK */

/**
 * Initialize this module.
 * 
 * Carves out memp_memory into linked lists for each pool-type.
 */
void
memp_init(void)
{
  struct memp *memp;
  u16_t i, j;

#if MEMP_STATS
  for (i = 0; i < MEMP_MAX; ++i) {
    lwip_stats.memp[i].used = lwip_stats.memp[i].max =
      lwip_stats.memp[i].err = 0;
    lwip_stats.memp[i].avail = memp_num[i];
  }
#endif /* MEMP_STATS */

  memp = LWIP_MEM_ALIGN(memp_memory);
  /* for every pool: */
  for (i = 0; i < MEMP_MAX; ++i) {
    memp_tab[i] = NULL;
    /* create a linked list of memp elements */
    for (j = 0; j < memp_num[i]; ++j) {
      memp->next = memp_tab[i];
      memp_tab[i] = memp;
      memp = (struct memp *)((u8_t *)memp + MEMP_SIZE + memp_sizes[i]);
    }
  }
#if MEMP_OVERFLOW_CHECK
  memp_overflow_init();
  /* check everything a first time to see if it worked */
  memp_overflow_check_all();
#endif /* MEMP_OVERFLOW_CHECK */
}

/**
 * Get an element from a specific pool.
 *
 * @param type the pool to get an element from
 * @param file file name calling this function
 * @param line number of line where this function is called
 */
void *
#if MEMP_OVERFLOW_CHECK
memp_malloc_fn(memp_t type, const char* file, const int line)
#else
memp_malloc(memp_t type)
#endif
{
  struct memp *memp;
  SYS_ARCH_DECL_PROTECT(old_level);
 
  LWIP_ERROR("memp_malloc: type < MEMP_MAX", (type < MEMP_MAX), return NULL;);

  SYS_ARCH_PROTECT(old_level);
#if MEMP_OVERFLOW_CHECK >= 2
  memp_overflow_check_all();
#endif /* MEMP_OVERFLOW_CHECK >= 2 */

  memp = memp_tab[type];
  
  if (memp != NULL) {    
    memp_tab[type] = memp->next;    
#if MEMP_OVERFLOW_CHECK
    memp->next = NULL;
    memp->file = file;
    memp->line = line;
#endif /* MEMP_OVERFLOW_CHECK */
#if MEMP_STATS
    ++lwip_stats.memp[type].used;
    if (lwip_stats.memp[type].used > lwip_stats.memp[type].max) {
      lwip_stats.memp[type].max = lwip_stats.memp[type].used;
    }
#endif /* MEMP_STATS */
    LWIP_ASSERT("memp_malloc: memp properly aligned",
                ((mem_ptr_t)memp % MEM_ALIGNMENT) == 0);
    memp = (struct memp*)((u8_t*)memp + MEMP_SIZE);
  } else {
    LWIP_DEBUGF(MEMP_DEBUG | 2, ("memp_malloc: out of memory in pool %"S16_F"\n", type));
#if MEMP_STATS
    ++lwip_stats.memp[type].err;
#endif /* MEMP_STATS */
  }

  SYS_ARCH_UNPROTECT(old_level);

  return memp;
}

/**
 * Put an element back into its pool.
 *
 * @param type the pool where to put mem
 * @param mem the memp element to free
 */
void
memp_free(memp_t type, void *mem)
{
  struct memp *memp;
  SYS_ARCH_DECL_PROTECT(old_level);

  if (mem == NULL) {
    return;
  }
  LWIP_ASSERT("memp_free: mem properly aligned",
                ((mem_ptr_t)mem % MEM_ALIGNMENT) == 0);

  memp = (struct memp *)((u8_t*)mem - MEMP_SIZE);

  SYS_ARCH_PROTECT(old_level);
#if MEMP_OVERFLOW_CHECK
#if MEMP_OVERFLOW_CHECK >= 2
  memp_overflow_check_all();
#else
  memp_overflow_check_element(memp, memp_sizes[type]);
#endif /* MEMP_OVERFLOW_CHECK >= 2 */
#endif /* MEMP_OVERFLOW_CHECK */

#if MEMP_STATS
  lwip_stats.memp[type].used--; 
#endif /* MEMP_STATS */
  
  memp->next = memp_tab[type]; 
  memp_tab[type] = memp;

#if MEMP_SANITY_CHECK
  LWIP_ASSERT("memp sanity", memp_sanity());
#endif /* MEMP_SANITY_CHECK */

  SYS_ARCH_UNPROTECT(old_level);
}
