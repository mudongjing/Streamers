#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <net_helper.h>
#include <topmanager.h>
#include <msg_types.h>
#include <peerset.h>

#include "streaming.h"
#include "loop.h"

#define BUFFSIZE 64 * 1024
static struct timeval period = {0, 500000};
static struct timeval tnext;

void tout_init(struct timeval *tv)
{
  struct timeval tnow;

  if (tnext.tv_sec == 0) {
    gettimeofday(&tnext, NULL);
  }
  gettimeofday(&tnow, NULL);
  if(timercmp(&tnow, &tnext, <)) {
    timersub(&tnext, &tnow, tv);
  } else {
    *tv = (struct timeval){0, 0};
  }
}

// currently it just makes the peerset grow
void update_peers(struct peerset *pset, const uint8_t *buff, int len)
{
  int n_ids;
  const struct nodeID **ids;
  topParseData(buff, len);
  ids = topGetNeighbourhood(&n_ids);
  peerset_add_peers(pset,ids,n_ids);
}


void loop(struct nodeID *s, int csize, int buff_size)
{
  int done = 0;
  static uint8_t buff[BUFFSIZE];
  int cnt = 0;
  struct peerset *pset = peerset_init(0);
  
  period.tv_sec = csize / 1000000;
  period.tv_usec = csize % 1000000;
  
  update_peers(pset, NULL, 0);
  stream_init(buff_size, s);
  while (!done) {
    int len, res;
    struct timeval tv;

    tout_init(&tv);
    res = wait4data(s, tv);
    if (res > 0) {
      struct nodeID *remote;

      len = recv_from_peer(s, &remote, buff, BUFFSIZE);
      switch (buff[0] /* Message Type */) {
        case MSG_TYPE_TOPOLOGY:
          update_peers(pset, buff, len);
          break;
        case MSG_TYPE_CHUNK:
          received_chunk(buff, len);
          break;
        default:
          fprintf(stderr, "Unknown Message Type %x\n", buff[0]);
      }
      free(remote);
    } else {
      struct timeval tmp;

      send_chunk(pset);
      if (cnt++ % 10 == 0) {
        update_peers(pset,NULL, 0);
      }
      timeradd(&tnext, &period, &tmp);
      tnext = tmp;
    }
  }
}

void source_loop(const char *fname, struct nodeID *s, int csize, int chunks)
{
  int done = 0;
  static uint8_t buff[BUFFSIZE];
  int cnt = 0;
  struct peerset *pset = peerset_init(0);

  period.tv_sec = csize  / 1000000;
  period.tv_usec = csize % 1000000;
  
  source_init(fname, s);
  while (!done) {
    int len, res;
    struct timeval tv;

    tout_init(&tv);
    res = wait4data(s, tv);
    if (res > 0) {
      struct nodeID *remote;

      len = recv_from_peer(s, &remote, buff, BUFFSIZE);
      switch (buff[0] /* Message Type */) {
        case MSG_TYPE_TOPOLOGY:
          fprintf(stderr, "Top Parse\n");
          update_peers(pset, buff, len);
          break;
        case MSG_TYPE_CHUNK:
          fprintf(stderr, "Some dumb peer pushed a chunk to me!\n");
          break;
        default:
          fprintf(stderr, "Bad Message Type %x\n", buff[0]);
      }
      free(remote);
    } else {
      int i;
      struct timeval tmp;

      generated_chunk();
      for (i = 0; i < chunks; i++) {	// @TODO: why this cycle?
        send_chunk(pset);
      }
      if (cnt++ % 10 == 0) {
          update_peers(pset, NULL, 0);
      }
      timeradd(&tnext, &period, &tmp);
      tnext = tmp;
    }
  }
}
